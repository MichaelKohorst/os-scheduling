#include "process.h"

// Process class methods
Process::Process(ProcessDetails details, uint64_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }
    is_interrupted = false;
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    remain_time = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        remain_time += burst_times[i];
    }
}

Process::~Process()
{
    delete[] burst_times;
}


uint32_t Process::getBurstTimes(uint16_t burst_index) const {
    return burst_times[burst_index];
}

void Process::setBurstIndex(uint16_t index) {
    current_burst = index;
}

uint16_t Process::getBurstIndex() const {
    return current_burst;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint32_t Process::getStartTime() const
{
    return start_time;
}

uint8_t Process::getPriority() const
{
    return priority;
}

uint64_t Process::getBurstStartTime() const
{
    return burst_start_time;
}

Process::State Process::getState() const
{
    return state;
}

bool Process::isInterrupted() const
{
    return is_interrupted;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

void Process::updateCpuTime(uint32_t new_time) {
    cpu_time = new_time;
}

double Process::getRemainingTime() const
{
    return (double)remain_time / 1000.0;
}

void Process::setBurstStartTime(uint64_t current_time)
{
    burst_start_time = current_time;
}

void Process::setState(State new_state, uint64_t current_time)
{
    if (state == State::NotStarted && new_state == State::Ready)
    {
        launch_time = current_time;
    }
    state = new_state;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::interrupt()
{
    is_interrupted = true;
}

void Process::interruptHandled()
{
    is_interrupted = false;
}

void Process::updateProcess(uint64_t current_time)
{
    // use `current_time` to update turnaround time, wait time, burst times, 
    // cpu time, and remaining time
    remain_time = (uint32_t)current_time;

}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}


// Comparator methods: used in std::list sort() method
   
   std::vector<Process*> processes;
   SchedulerConfig *config = readConfigFile(argv[1]);
   uint64_t start = currentTime();
   uint64_t current = currentTime();
   uint64_t end = start + getRemainingTime();
   int i;
// No comparator needed for FCFS or RR (ready queue never sorted)

// SJF - comparator for sorting read queue based on shortest remaining CPU time
bool SjfComparator::operator ()(const Process *p1, const Process *p2)
{
    for(i = 0; i < config->numprocesses; i++){ 
       //first create processes based on total remaining cPU time
       if(p1->getRemainingTime()*1000 > p2->getRemainingTime()*1000){
          *p2 = new Process(config->process[i], start);
          *p1 = new Process(config->process[i], end);
          //processes.push_back(p2);
          //processes.push_back(p1);
       }
       else if(p2->getRemainingTime()*1000 > p1->getRemainingTime()*1000){ 
         *p1 = new Process(config->process[i], start);
         *p2 = new Process(config->process[i], end);
         //processes.push_back(p1);
         //processes.push_back(p2);
       }
       //create processes based on I/O bursts
       else if(p2->getBurstTimes(p1->getBurstIndex()) <= 0){
           start = p2->getRemainingTime();
           end = p1->getRemainingTime();
     	   *p2 = new Process(config->processes[i], start);
     	   *p1 = new Process(config->process[i], end);
       }
       else{
           start = p1->getRemainingTime();
           end = p2->getRemainingTime();
           *p1 = new Process(config->processes[i], start);
     	   *p2 = new Process(config->process[i], end);
       }
    }
   
    return true; // change this!
}

// PP - comparator for sorting read queue based on priority
bool PpComparator::operator ()(const Process *p1, const Process *p2)
{
    for(i = 0; i < config->numprocesses; i++){ 
       //checking the priority
       if(p1->getPriority() < p2->getPriority()){
          start = p1->getRemainingTime();
          end = p2->getRemainingTime();
          *p1 = new Process(config->processes[i], start);
          *p2 = new Process(config->process[i], end);
          
          if(p1->getPriority == p2->getPriority()){
              SjfComparator(p1,p2);
           }
       }
       else if(p2->getPriority() < p1->getPriority()){
          start = p2->getRemainingTime();
          end = p1->getRemainingTime();
          *p2 = new Process(config->processes[i], start);
          *p1 = new Process(config->process[i], end);
          if(p1->getPriority == p2->getPriority()){
              SjfComparator(p1,p2);
          }
       }

       // I/O bursts
       else if(p1->getBurstTimes(p1->getBurstIndex()) <= 0){
    	   if(p1->getPriority() < p2->getPriority()){
             start = p1->getRemainingTime();
             end = p2->getRemainingTime();
             *p1 = new Process(config->processes[i], start);
             *p2 = new Process(config->process[i], end);
           }
           if(p1->getPriority == p2->getPriority()){
              SjfComparator(p1,p2);
           }
       }
       else if(p2->getBurstTimes(p2->getBurstIndex()) <= 0){
           if(p2->getPriority() < p1->getPriority()){
             start = p2->getRemainingTime();
             end = p1->getRemainingTime();
             *p2 = new Process(config->processes[i], start);
             *p1 = new Process(config->process[i], end);
           }
           if(p1->getPriority == p2->getPriority()){
              SjfComparator(p1,p2);
           }
           // if finish I/O burst and higher priority, check cpucore and see if it has a higher priority
           // than the process currently running
           if(p1->getState() == Process::State::IO){
        	   if(p1->getBurstTimes(p1->getBurstIndex()) <= 0 && p1->getPriority() < p2->getPriority()){
        		   if(p2->getState() == Process::State::Running){
        		   	p2->interrupt();
        			//do i pop to remove from CPU
        		  	start == p1->getRemainingTime();
        			*p1 = new Process(config->processes[i], start);
        		   }
        	   }
        
           }
           if(p2->getState() == Process::State::IO){
        	   if(p2->getBurstTimes(p2->getBurstIndex()) <= 0 && p2->getPriority() < p1->getPriority()){
        		   if(p1->getState() == Process::State::Running){
        			   p1->interrupt();
        			   //do i pop to remove from CPU
        			   start == p2->getRemainingTime();
        			   *p2 = new Process(config->processes[i], start);
        		   }
        	   }


          }

      }

  }

    return true; // change this!


}
