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

//void calculatePercentage(totalCpuTime);
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

        if (p->getStartTime() > 0) {
            p->setState(Process::State::NotStarted, currentTime());
        }
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
            p->setTimeProcessStarted(currentTime());
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
            if (processes[i]->getStartTime()  <= currTime-start && processes[i]->getState() == Process::State::NotStarted){
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->setState(Process::State::Ready, currTime);
                shared_data->ready_queue.push_back(processes[i]);
                processes[i]->setTimeProcessStarted(currTime);
            }
        }

        //   - *Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
         for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getState() == Process::State::IO && (currTime - processes[i]->getBurstStartTime() >= processes[i]->getBurstTimes(processes[i]->getBurstIndex())))  
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->setState(Process::State::Ready, currTime);
                shared_data->ready_queue.push_back(processes[i]);
                //processes[i]->setBurstIndex(processes[i]->getBurstIndex()+1);
            }
        }

        //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
        for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getState() == Process::State::Running && shared_data->algorithm == ScheduleAlgorithm::RR && (currTime - processes[i]->getBurstStartTime() >= shared_data->time_slice))  
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                processes[i]->interrupt();
            }
        }
        int lowestPriority = 0;
        int lowestPriorityProcessRunning = 0;
         for (int i = 0; i < processes.size(); i++)
         {
             if(processes[i]->getState() == Process::State::Running && processes[i]->getPriority() > lowestPriority)
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

        if(!shared_data->ready_queue.empty())
        {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            if(shared_data->algorithm == ScheduleAlgorithm::PP)
            {
                PpComparator pp;
                shared_data->ready_queue.sort(pp);
            }

            else if(shared_data->algorithm == ScheduleAlgorithm::SJF)
            {
                SjfComparator sjf;
                shared_data->ready_queue.sort(sjf);
            }  
        }

        for (int i = 0; i < processes.size(); i++) {
            Process* currProcess = processes[i];
            if (currProcess->getState() != Process::State::Terminated && currProcess->getState() != Process::State::NotStarted) {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                uint32_t savedTurnaroundTime = currProcess->getTurnaroundTime()*1000;
                uint32_t timeStarted = currProcess->getTimeProcessStarted();
                currProcess->updateTurnTime(currentTime()-timeStarted);
            }
            if (currProcess->getRemainingTime() <= 0) {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                currProcess->setState(Process::State::Terminated, currentTime());
            }
            if (currProcess->getState() == Process::State::Ready) {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                int32_t savedWaitTime = currProcess->getWaitTime();
                uint32_t timeStarted = currProcess->getTimeProcessStarted();
                currProcess->updateWaitTime((currentTime() - timeStarted) + savedWaitTime);
            }
        }

        //   - Determine if all processes are in the terminated state
        //   - * = accesses shared data (ready queue), so be sure to use proper synchronization

        isTerminated(shared_data, processes);
        if (shared_data->all_terminated == true) {
            exit(0);
        }

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 50 ms
        usleep(50000);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

    // print final statistics
    //uint32_t throughputFirst = calculatePercentage(totalCpuTime) / 2;
    //  - CPU utilization
    //printf("the total percentage of tiem the CPU is computing %d", calculatePercentage(totalCpuTime));
    //  - Throughput
    //     - Average for first 50% of processes finished
    printf("the average number of processs that finsihed in the first half of the CPU computing time is ");
    //     - Average for second 50% of processes finished
    printf("the average number of processs that finsihed in the second half of the CPU computing time is ");
    //     - Overall average
    printf("the total average of processes that finished for the CPU computing time is ");
    //  - Average turnaround time
    printf("The average turnaround time is");
    //  - Average waiting time
    printf("The average wait time is");


    // Clean up before quitting program
    processes.clear();

    return 0;
}

//void calculatePercentage(totalCpuTime){
//	totalCpuTime / 100;
//}

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
            uint32_t savedTurnaroundTime = currProcess->getTurnaroundTime()*1000;
            

            currProcess->setState(Process::State::Running, currentTime());
            currProcess->setBurstStartTime(start);

            while(!currProcess->isInterrupted() && currProcess->getState() == Process::State::Running) {
                currProcess->setCpuCore(core_id);
                if (currProcess->getRemainingTime()*1000 <= 0) {
                    //Terminate
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    currProcess->setState(Process::State::Terminated, currentTime());
                    currProcess->updateProcess(0);
                    currTime = currentTime();
                    currProcess->updateTurnTime((currTime-start)+savedTurnaroundTime);
                } else {
                    currTime = currentTime();

                    if (currProcess->getBurstTimes(currProcess->getBurstIndex()) <= 0) {
                        currProcess->setState(Process::State::IO, currentTime());
                        currProcess->setBurstIndex(currProcess->getBurstIndex()+1);
                        currProcess->updateTurnTime((currTime-start)+savedTurnaroundTime);
                    } else {
                        if ((int32_t)currProcess->getBurstTimes(currProcess->getBurstIndex()) - (int32_t)(currTime-start-alreadyRemovedTime) <= 0) {
                            currProcess->updateBurstTime(currProcess->getBurstIndex(), 0);
                        } else {
                            currProcess->updateBurstTime(currProcess->getBurstIndex(), currProcess->getBurstTimes(currProcess->getBurstIndex()) - (currTime-start-alreadyRemovedTime));
                        }
                        currProcess->updateProcess((currProcess->getRemainingTime()*1000) - (currTime-start-alreadyRemovedTime));
                        alreadyRemovedTime = currTime-start;
                        currProcess->updateCpuTime(savedCpuTime+(currTime-start));
                        currProcess->updateTurnTime((currTime-start)+savedTurnaroundTime);
                    }
                }
                //usleep(5000);
            }
            currProcess->setCpuCore(-1);
        }
        //usleep(5000);
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
