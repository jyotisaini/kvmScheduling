# Cpu and Memory Schedular in virtualized environments

VCPU Schedular

In general, vCPUs in libvirt are mapped to all physical CPUs in the hypervisor by default. Our scheduler decides to pin vCPUs to pCPUs so that pCPU usage is balanced, while making as few 'pin changes' as possible as these are costly.

Compile

Run make in the root of this project, it will compile vcpu_scheduler.c into bin/vcpu_scheduler
Usage

./bin/vcpu_scheduler [PERIOD IN SECONDS]

    The scheduler runs on a period determined by the argument passed (in seconds)
    ./vcpu_scheduler 3 would run the scheduler every 3 seconds
    You need to have active libvirt domains in order to run the scheduler.
    The scheduler will stop and close on the first cycle without active domains.

The scheduling algorithm works around only one fairness principle: ensure the pCPUs usage is as balanced as possible.

To do so, it changes the vCPU affinity and pinning of pCPU to vCPU according to the following algorithm:

On every scheduler period it calculates:
       vCPU usage (%) for all domains
       Usage (%) for all pCPUs [1] -> calculated by summation of each vcpu usage on that pcpu.
       Busiest pCPU (highest usage)
       Freest pCPU (lowest usage)
Once we have this information, the fairness algorithm is applied using pinPcpus.
        if the (busiest pcpu - freeest pcpu usage ) is less than 10% we say the pcpus are balanced.
        but if its > 10 we try to balance the pcpus. we take vcpu from the busiest pcpu and pin it to freest pcpu. 
        If busiest Pcpu had only once VCPU we dont change the pins as this vcpu alone has all the load, so we let that vcpu be on the single pcpu.


[1] Since libvirt won't give you the %, we have to calculate it by taking samples of the cputime on every period. Say we have a scheduling period of 10ns:

    When the program starts, take sample of pCPUtime, let's say it's 500ns
    After 10ns, we take another sample - it's 505ns.
    We can use the samples and the period to infer the usage, (505-500)/10 = 0.5 -> 50% usage

Stress Testing

    Create virtual machines with virt-manager.
    Once they have loaded, install stress
    Run stress -c N where N is the number of workers you want to have spinning on sqrt, each of them wasting as much CPU as possible.
    htop is one of the tools that I used to ensure the vcpu_scheduler usage numbers were correct. Open it in a guest and check that vcpu_scheduler results are the same.

Memory coordinator

This program aims to balance the unused memory in every domain, to ensure fairness among VMs, and to waste as little hypervisor memory as possible.

Compile

Run make in the root of this project, it will compile Memory_coordinator.c into bin/Memory_coordinator
Usage

./bin/Memory_coordinator [PERIOD IN SECONDS]

    The coordinator runs on a period determined by the argument passed (in seconds)
    Memory_coordinator 3 would run the coordinator every 3 seconds
    You need to have active libvirt domains in order to run the coordinator.
    The coordinator will stop and close on the first cycle without active domains.

The coordination algorithm works around two fairness principles:

    Give back memory from the wasteful domains to the hypervisor
    Assign memory from the hypervisor to the starved domains until they are stable.

A Domain is starved when it has less than 15% of total Memory assigned left and is still consuming at a > 1 rate of comsumption. 
A Domain is considered to be wasting memory when it has more than 50% of total memory available and has < 1 as  rate of consumption of memory.
A Domain is considered to be balanced if it has less 50% of total memory as unused memory. 

The algorithm used to calculate fairness is the following:

    On every coordination period, we calculate the stats for each domain. We find total memory, unsed memory and percentage unsed memory for each domain and populate in DomainMemoryStats.
    Once Memory stats are calculated, we calculate the DomainRatestats i.e rate of consumption of each domain by taking a diff from previous stats and current stats and diving by time interval and then assign each domain a status (BALANCED, STARVING, WASTING)

    We define different thresholds as follows 

    STARVATION_MEMORY_THRESHOLD = 15;  -> percent of total domain memory left unused below which domain is starving

    WASTE_MEMORY_THRESHOLD = 50; -->  percent of total domain memory left unused above which domain is wasting memory.

    HYPERVISOR_UNUSED_MEMORY_THRESHOLD = 5; - minimum percent of host memroy requirement for Hypervisor. while assigning memory from hypervisor to domain we check if after removing memory from hypervisor, percent hypervisor ununsed memory left is greater than HYPERVISOR_UNUSED_MEMORY_THRESHOLD. If its not, we donot take memory away from hypervisor as this will freeze the host.

    MIN_MEMORY_FOR_DOMAIN = 50*1024; -> This threshold is used while removing memory from domain and giving to hypervisor. we check if after removing memory, domain memory is less than this threshold or not. if it goes below then we donot remove memory from domain. because taking most of memory from host might kill the host.


    Now We Find the most starved and the most wasteful domains.
         if there is starved domain and wastful domain, we take  2*rateofConsumption of memory by startved domain from wasteful domain and give it to starved domain. We check if removing memory from wasteful might lead to domian memeory below 50MB or adding memory to domain may lead to memory assigned more than maxMemory for that domain. We can not assign memory more than maxMemory for each domain.


        if there is starved domain but no wastful domain, We take 2*rateofConsumption of memory by startved domain from hypervisor and give it to starved domain. 
        If there is a memory wasting memory above the waste threshold, halve that domain's memory and assign the same amount to the starved domain.We check if removing memory from host may or may not lead to memory less than 5% left. In such case we donot remove memory from host as it will freeze the host.


        If there is no starving domain but wasteful domains, we give memory from wasteful domain to hypervisor chekcing all the threshold conditions.

        The last case, if there is no starved domain nor wasteful domains don't do anything.
    This is repeated until there are no active domains

Stress testing

    Create virtual machines with virt-manager.
    Once they have loaded, install stress
    Run stress -m N where N is the number of workers you want to have spinning on malloc, each of them wasting 256MB.
    You will notice that once your VMs become memory starved, the coordinator will start serving your VMs memory from either the host or 'memory wasteful' VMs
    You can play with the threshold by modifying the STARVATION_MEMORY_THRESHOLD, WASTE_MEMORY_THRESHOLD , HYPERVISOR_UNUSED_MEMORY_THRESHOLD and MIN_MEMORY_FOR_DOMAIN constants in the source and recompiling the program.
