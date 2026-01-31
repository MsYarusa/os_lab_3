#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define getpid _getpid
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <semaphore.h>
#endif

const char* LOG_FILE = "lab_3.log";
const char* SHM_NAME = "/lab_3_shared_mem";
const char* SEM_MASTER = "/lab_3_master_sem";
const char* MUTEX_COUNTER = "lab_3_counter_mutex";

struct SharedData {
    long long counter;
    long long master_pid;
};

SharedData* shared_data = nullptr;
bool is_master = false;

#ifdef _WIN32
    HANDLE hMapFile = NULL;
    HANDLE hCounterMutex = NULL;
    HANDLE hMasterMutex = NULL;
    PROCESS_INFORMATION cp1 = {0}, cp2 = {0};
#else
    int shm_fd = -1;
    sem_t* sem_master = nullptr;
    pid_t cp1 = 0, cp2 = 0;
#endif

std::string get_time_str() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt = *std::localtime(&timer);
    
    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void write_log(const std::string& message) {
    std::ofstream log(LOG_FILE, std::ios::app);
    
    if (log.is_open()) {
        log << "[" << get_time_str() << "] [PID: " << getpid() << "] " << message << std::endl;
    }
}

enum OpMode { ADD, MULTIPLY, DIVIDE };

void modify_counter(long long delta, OpMode mode = ADD) {
#ifdef _WIN32
    WaitForSingleObject(hCounterMutex, INFINITE);
    
    if (mode == MULTIPLY) shared_data->counter *= delta;
    else if (mode == DIVIDE) shared_data->counter /= delta;
    else shared_data->counter += delta;
    
    ReleaseMutex(hCounterMutex);
#else
    long long current = shared_data->counter;
    long long value_to_add = delta;

    if (mode == MULTIPLY) value_to_add = (current * delta) - current;
    else if (mode == DIVIDE) value_to_add = (current / delta) - current;

    __sync_fetch_and_add(&shared_data->counter, value_to_add);
#endif
}

#ifdef _WIN32
void spawn_copy(const std::string& arg, PROCESS_INFORMATION& pi) {
    char cmd[MAX_PATH];
    GetModuleFileNameA(NULL, cmd, MAX_PATH);
    std::string cmdLine = std::string(cmd) + " " + arg;
    
    STARTUPINFOA si = { sizeof(si) };
    CreateProcessA(NULL, (char*)cmdLine.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
}

bool is_running(PROCESS_INFORMATION& pi) {
    if (pi.hProcess == NULL) return false;
    DWORD code;
    GetExitCodeProcess(pi.hProcess, &code);
    return code == STILL_ACTIVE;
}
#else
pid_t spawn_copy(const std::string& arg) {
    pid_t pid = fork();
    if (pid == 0) {
        char cmd[1024];
        ssize_t len = readlink("/proc/self/exe", cmd, sizeof(cmd) - 1);
        if (len != -1) {
            cmd[len] = '\0';
            execl(cmd, cmd, arg.c_str(), (char*)NULL);
        }
        _exit(0);
    }
    return pid;
}

bool is_running(pid_t pid) {
    if (pid <= 0) return false;
    return waitpid(pid, NULL, WNOHANG) == 0;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedData), "Local\\Lab3Shm");
    shared_data = (SharedData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    hCounterMutex = CreateMutexA(NULL, FALSE, "Local\\Lab3CounterMutex");
    hMasterMutex = CreateMutexA(NULL, FALSE, "Local\\Lab3MasterMutex");
#else
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
   
    struct stat st;
    fstat(shm_fd, &st);
    if (st.st_size == 0) {
        ftruncate(shm_fd, sizeof(SharedData));

        shared_data = (SharedData*)mmap(0, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        memset(shared_data, 0, sizeof(SharedData));
    } else {
        shared_data = (SharedData*)mmap(0, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    }
    sem_master = sem_open(SEM_MASTER, O_CREAT, 0666, 1);
#endif

    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--copy1") {
            write_log("Copy 1 started.");
            modify_counter(10);
            write_log("Copy 1 exiting.");
            return 0;
        } else if (mode == "--copy2") {
            write_log("Copy 2 started.");
            modify_counter(2, MULTIPLY); // * 2
            std::this_thread::sleep_for(std::chrono::seconds(2));
            modify_counter(2, DIVIDE);
            write_log("Copy 2 exiting.");
            return 0;
        }
    }

    write_log("Main instance started.");

    std::thread input_thread([]() {
        long long val;
        while (true) {
            if (std::cin >> val) {
                modify_counter(val - shared_data->counter);
                std::cout << "Counter set to " << val << std::endl;
            }
        }
    });
    input_thread.detach();

    auto last_300ms = std::chrono::steady_clock::now();
    auto last_1s = std::chrono::steady_clock::now();
    auto last_3s = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_300ms).count() >= 300) {
            modify_counter(1);
            last_300ms = now;
        }

#ifdef _WIN32
        is_master = (WaitForSingleObject(hMasterMutex, 0) == WAIT_OBJECT_0);
#else
   if (!is_master) {
        if (sem_trywait(sem_master) == 0) {
            is_master = true;
            shared_data->master_pid = getpid();
            write_log("I am the new Master!");
        } else {
            long long current_master_pid = shared_data->master_pid;

            if (current_master_pid > 0 && kill(current_master_pid, 0) == -1) {
                
                if (__sync_bool_compare_and_swap(&shared_data->master_pid, current_master_pid, getpid())) {
                    
                    write_log("Detected dead master (" + std::to_string(current_master_pid) + "). I am taking over!");
                    
                    int val;
                    sem_getvalue(sem_master, &val);
                    while (val <= 0) {
                        sem_post(sem_master);
                        sem_getvalue(sem_master, &val);
                    }
                    
                    sem_trywait(sem_master);
                    is_master = true;
                }
            }
        }
    } else {
        shared_data->master_pid = getpid();
    }
#endif

        if (is_master) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_1s).count() >= 1) {
                write_log("Periodic log. Counter: " + std::to_string(shared_data->counter));
                last_1s = now;
            }

            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_3s).count() >= 3) {
                if (is_running(cp1) || is_running(cp2)) {
                    write_log("Copies still running. Skipping spawn.");
                } else {
                    write_log("Spawning copies.");
#ifdef _WIN32
                    spawn_copy("--copy1", cp1);
                    spawn_copy("--copy2", cp2);
#else
                    cp1 = spawn_copy("--copy1");
                    cp2 = spawn_copy("--copy2");
#endif
                }
                last_3s = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

#ifndef _WIN32
    if (is_master) {
        sem_close(sem_master);
        sem_unlink(SEM_MASTER);
        shm_unlink(SHM_NAME);
    }
#endif

    return 0;
}