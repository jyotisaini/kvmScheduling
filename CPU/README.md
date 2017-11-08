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

