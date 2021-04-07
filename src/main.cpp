#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
} SchedulerData;

void isTerminated(SchedulerData *shared_data, std::vector<Process*> processes);
void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint64_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char **argv)
{
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    // Store configuration parameters in shared data object
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
        }
    }

    // Free configuration data from memory
    deleteConfig(config);

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

     // Main thread work goes here
    int num_lines = 0;
    while (!(shared_data->all_terminated))
    {
        // Clear output from previous iteration
        clearOutput(num_lines);

        // Do the following:
        //   - Get current time
        uint64_t currTime = currentTime();
        //   - *Check if any processes need to move from NotStarted to Ready (based on elapsed time), and if so put that process in the ready queue
         for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getStartTime()  >= currTime-start && processes[i]->getState() == Process::State::NotStarted){
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->setState(Process::State::Ready, currTime);
                shared_data->ready_queue.push_back(processes[i]);
            }
        }

        //   - *Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
         for (int i = 0; i < processes.size(); i++) {
            if (processStateToString(processes[i]->getState()).compare("i/o") && (currTime - processes[i]->getBurstStartTime() >= processes[i]->getBurstTimes(processes[i]->getBurstIndex())))  
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->setState(Process::State::Ready, currTime);
                shared_data->ready_queue.push_back(processes[i]);
            }
        }

        //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
        for (int i = 0; i < processes.size(); i++) {
            if (processStateToString(processes[i]->getState()).compare("running") && shared_data->algorithm == ScheduleAlgorithm::RR && (currTime - processes[i]->getBurstStartTime() >= shared_data->time_slice))  
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->interrupt();
            }
        }
        int lowestPriority = 0;
        int lowestPriorityProcessRunning = 0;
         for (int i = 0; i < processes.size(); i++)
         {
             if(processStateToString(processes[i]->getState()).compare("running") && processes[i]->getPriority() > lowestPriority)
             {
                 lowestPriority = processes[i]->getPriority();
                 lowestPriorityProcessRunning = i;
             }
         }

        if(shared_data->algorithm == ScheduleAlgorithm::PP)
        {
            Process* frontQueueProcess = NULL;
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            if (shared_data->ready_queue.size() != 0) {
                frontQueueProcess = shared_data->ready_queue.front();
                if(frontQueueProcess->getPriority() > lowestPriority)
                {
                    processes[i]->interrupt();
                }
            }       
        }

        //   - *Sort the ready queue (if needed - based on scheduling algorithm)
        std::list<Process*> tempQueue;

        if(shared_data->algorithm == ScheduleAlgorithm::PP)
        {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            lowestPriority = 4;
            lowestPriorityProcessRunning = 0;
            std::list<Process* >::iterator it = shared_data->ready_queue.begin(); 
            for(int i = 0; i < shared_data->ready_queue.size(); i++)
            {
                std::list<Process* >::iterator it = shared_data->ready_queue.begin();
                
            }
        }

        else if(shared_data->algorithm == ScheduleAlgorithm::SJF)
        {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            for(int i = 0; i < shared_data->ready_queue.size(); i++)
            {
                
            }
        }

        //   - Determine if all processes are in the terminated state
        //   - * = accesses shared data (ready queue), so be sure to use proper synchronization

        for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getState() == Process::State::IO) {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->setState(Process::State::Ready, currentTime());
                shared_data->ready_queue.push_back(processes[i]);
                //std::cout << shared_data->ready_queue.size() << "\n";
                processes[i]->setBurstIndex(processes[i]->getBurstIndex()+1);
                //std::cout << "Processes size: " << processes.size() << "\n";
            }
        }

        isTerminated(shared_data, processes);
        if (shared_data->all_terminated == true) {
            exit(0);
        }

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 50 ms
        usleep(5000);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

    // print final statistics
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
    //  - Average waiting time


    // Clean up before quitting program
    processes.clear();

    return 0;
}

void isTerminated(SchedulerData *shared_data, std::vector<Process*> processes) {
    std::lock_guard<std::mutex> lock(shared_data->mutex);
    int count = 0;
    for (int i = 0; i < processes.size(); i++) {
        if (processes[i]->getState() == Process::State::Terminated) {
            count == count++;
        }
    }
    if (count == processes.size()) {
        shared_data->all_terminated = true;
        exit(0);
    }
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    // Work to be done by each core independent of the other cores
    // Repeat until all processes in terminated state:
    //   - *Get process at front of ready queue
    //   - Simulate the processes running until one of the following:
    //     - CPU burst time has elapsed
    //     - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
    //  - Place the process back in the appropriate queue
    //     - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
    //     - Terminated if CPU burst finished and no more bursts remain -- no actual queue, simply set state to Terminated
    //     - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)
    //  - Wait context switching time
    //  - * = accesses shared data (ready queue), so be sure to use proper synchronization

    while (shared_data->all_terminated != true) {
        Process* currProcess = NULL;
        {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            if (shared_data->ready_queue.size() != 0) {
                currProcess = shared_data->ready_queue.front();
                shared_data->ready_queue.pop_front();
            }
        }
        if (currProcess != NULL) {
            uint64_t start = currentTime();
            uint64_t currTime = currentTime();
            uint64_t alreadyRemovedTime = 0;
            uint32_t savedCpuTime = currProcess->getCpuTime()*1000;
            std::cout << "SAVED: " << savedCpuTime*1000;

            currProcess->setState(Process::State::Running, currentTime());
            currProcess->setBurstStartTime(start);

            while(!currProcess->isInterrupted() && currProcess->getState() == Process::State::Running) {
                if (currProcess->getRemainingTime()*1000 <= 0) {
                    //Terminate
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    currProcess->setState(Process::State::Terminated, currentTime());
                    currProcess->updateProcess(0);
                } else {
                    currTime = currentTime();

                    if (currProcess->getBurstTimes(currProcess->getBurstIndex()) <= 0) {
                        currProcess->setState(Process::State::IO, currentTime());
                        currProcess->setBurstIndex(currProcess->getBurstIndex()+1);
                    } else {
                        if ((int32_t)currProcess->getBurstTimes(currProcess->getBurstIndex()) - (int32_t)(currTime-start-alreadyRemovedTime) <= 0) {
                            currProcess->updateBurstTime(currProcess->getBurstIndex(), 0);
                        } else {
                            currProcess->updateBurstTime(currProcess->getBurstIndex(), currProcess->getBurstTimes(currProcess->getBurstIndex()) - (currTime-start-alreadyRemovedTime));
                        }
                        currProcess->updateProcess((currProcess->getRemainingTime()*1000) - (currTime-start-alreadyRemovedTime));
                        alreadyRemovedTime = currTime-start;
                        currProcess->updateCpuTime(savedCpuTime+(currTime-start));
                        //std::cout << "CHANGED: " << currProcess->getBurstTimes(currProcess->getBurstIndex());
                    }
                }
                usleep(10000);
            }

            /*
            while (currTime - start < shared_data->time_slice && currTime - start < currProcess->getRemainingTime()) {
                std::cout << currProcess->getBurstTimes(0) << "\n";
                currTime = currProcess->getRemainingTime() - shared_data->time_slice/1000;
            }
            */
           
           /*
            if (currProcess->getRemainingTime()*1000 - shared_data->time_slice <= 0) {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                currProcess->setState(Process::State::Terminated, currentTime());
                currProcess->updateProcess(0);
            } else {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                currProcess->setState(Process::State::IO, currentTime());
                if (currProcess->getBurstTimes(currProcess->getBurstIndex()) < shared_data->time_slice) {
                    currProcess->updateProcess((currProcess->getRemainingTime()*1000) - currProcess->getBurstTimes(currProcess->getBurstIndex()));
                    currProcess->setBurstIndex(currProcess->getBurstIndex()+1);
                    currProcess->updateCpuTime(currProcess->getCpuTime()*1000 + currProcess->getBurstTimes(currProcess->getBurstIndex()));
                    uint32_t temp = currProcess->getCpuTime()*1000 + currProcess->getBurstTimes(currProcess->getBurstIndex());
                    std::cout << "FIRST: " << temp;
                } else {
                    currProcess->updateProcess((currProcess->getRemainingTime()*1000) - shared_data->time_slice);
                    currProcess->updateBurstTime(currProcess->getBurstIndex(), currProcess->getBurstTimes(currProcess->getBurstIndex()) - shared_data->time_slice);
                    currProcess->updateCpuTime(currProcess->getCpuTime()*1000 + shared_data->time_slice);
                    uint32_t temp = currProcess->getBurstTimes(currProcess->getBurstIndex());
                    std::cout << " SECOND: " << temp;
                }
            }
            */
        }
        usleep(10000000);
    }

} 

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;
    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }
    rewind(stdout);
    fflush(stdout);
}

uint64_t currentTime()
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
