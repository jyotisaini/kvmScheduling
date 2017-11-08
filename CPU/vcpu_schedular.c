#include<stdio.h>
#include<stdlib.h>
#include<../Utilities/libvirt_ext.c>
#include<unistd.h>

#define activeON 1
#define activeOFF 0
typedef struct domainStats {
	virDomainPtr domain;
	int vcpusCount;
	double *usage;
	double avgUsage;
	// Contains the vCPU time samples in the same order
	// as the vCPUs, e.g vcpus[0] for vCPU0
	unsigned long long int *vcpus;
} domainStats;
static const long NANOSECOND = 1000000000;
static const double USAGE_THRESHOLD = 50.0;

void fetchVcpuActiveDomains(domainList domainLists);
unsigned long long samplePcpuStats(virConnectPtr conn);
virDomainStatsRecordPtr *domainvCPUStats(domainList list);
double findUsage(unsigned long long difference, unsigned long long period);
void outputDomainParameters(virDomainStatsRecordPtr record);
void calculateDomainUsage(domainStats *previousDomainStats,
                          domainStats *currentDomainStats,
                          int domainsCount,
                          long period);
domainStats createdomainStats(virDomainStatsRecordPtr record);
void pinPcpus(int nrCpus,
              domainStats *domainStats, int domainsCount);

/* Main here */
int main(int argc, char **argv) {
  
{
	check(argc == 2, "ERROR: please pass only one argument i.e time interval in seconds");
	domainList list;
	domainStats *previousDomainStats, *currentDomainStats;
	int domainCounter = 0, previousCount = 0, maxcpus;
	unsigned long long previousPcpu, currentPcpu;
	virConnectPtr localConnection =  makeLocalConnection();
        virNodeInfo nodeInfo;
	virDomainStatsRecordPtr *domains = NULL;
        list = getDomains(localConnection, activeON); 
	printf(" vCPU scheduler  interval: %s\n", argv[1]);
	virNodeGetInfo(localConnection, &nodeInfo);
	// get previous PCPU smaple
	previousPcpu = samplePcpuStats(localConnection);
	maxcpus = virNodeGetCPUMap(localConnection, NULL, NULL, 0);

	while (list.totalCount > 0) {
		// pCPU usage calculation
		currentPcpu = samplePcpuStats(localConnection);
		printf("pCPU usage: %f%%\n",
		       findUsage(currentPcpu - previousPcpu,
			     atoi(argv[1]) * NANOSECOND)/nodeInfo.cpus);
		previousPcpu = currentPcpu;

		// vCPU usage calculation
		domains = domainvCPUStats(list);
		currentDomainStats = ( domainStats *)
			calloc(list.totalCount, sizeof( domainStats));
		virDomainStatsRecordPtr *next;

		for (next = domains; *next; next++) {
			currentDomainStats[domainCounter] = createdomainStats(*next);
			domainCounter++;
		}

		// Check if the number of VMs changed, if so, don't calculate
		if (previousCount != list.totalCount) {
			// Do not calculate usage or change pinning, we don't have stats yet
	                previousDomainStats = ( domainStats *)
				calloc(list.totalCount, sizeof( domainStats));
			memcpy(previousDomainStats, currentDomainStats,
			       list.totalCount * sizeof( domainStats));
		} else { 

            // calculate usage for each vpcu
			calculateDomainUsage(previousDomainStats,
					     currentDomainStats,
					     list.totalCount,
					     atoi(argv[1]) * NANOSECOND);
                        printf("pinning CPUS");
             // change pins on the basis of various conditions.
			pinPcpus(maxcpus,
				 currentDomainStats, list.totalCount);
		}

		domainCounter = 0;
		free(currentDomainStats);
		virDomainStatsRecordListFree(domains);
		previousCount = list.totalCount;
		sleep(atoi(argv[1]));
	}
	printf("No active domains - closing.\n");
	virConnectClose(localConnection);
	free(localConnection);
	return 0;
error:
	return 1;
 }

}

void fetchVcpuActiveDomains(domainList domainList) {
   for(int i =0; i <domainList.totalCount; i++ )
   {

      int vcpu = virDomainGetVcpusFlags(domainList.domains[i],VIR_DOMAIN_VCPU_MAXIMUM);
      printf("%s --> VCPUS ==> %d", virDomainGetName(domainList.domains[i]), vcpu);
   }
}

// Samples the global CPU time
unsigned long long samplePcpuStats(virConnectPtr conn)
{
	int nr_params = 0;
	int nrCpus = VIR_NODE_CPU_STATS_ALL_CPUS;
	virNodeCPUStatsPtr params;
	unsigned long long busy_time = 0;

	check(virNodeGetCPUStats(conn, nrCpus, NULL, &nr_params, 0) == 0 &&
	      nr_params != 0, "Could not get pCPU stats 1");
	params = malloc(sizeof(virNodeCPUStats) * nr_params);
	check(params != NULL, "Could not allocate pCPU params");
	memset(params, 0, sizeof(virNodeCPUStats) * nr_params);
	check(virNodeGetCPUStats(conn, nrCpus, params, &nr_params, 0) == 0,
	      "Could not get pCPU stats 2");
        //  printf("nr params ===> %d" , nr_params);
	for (int i = 0; i < nr_params; i++) {
		if (strcmp(params[i].field, VIR_NODE_CPU_STATS_USER) == 0 ||
		    strcmp(params[i].field, VIR_NODE_CPU_STATS_KERNEL) == 0) {
			busy_time += params[i].value;
		}
	}
	free(params);
	return busy_time;
error:
	exit(1);
}

virDomainStatsRecordPtr *domainvCPUStats(domainList list)
{
	unsigned int stats = 0;
	virDomainStatsRecordPtr *records = NULL;

	stats = VIR_DOMAIN_STATS_VCPU;
	check(virDomainListGetStats(list.domains, stats,
				    &records, 0) > 0,
	      "Could not get domains stats");
	
        /* ===========Debugging=========
	  virDomainStatsRecordPtr *next;
          for (next = records; *next; next++) {
	     outputDomainParameters(*next);
	}  
         =======Debugging ends===========  */
	return records;
error:
	exit(1);
}

double findUsage(unsigned long long difference, unsigned long long period)
{
	return 100 * ((double) difference / (double) period);
}

void outputDomainParameters(virDomainStatsRecordPtr record)
{
	for (int i = 0; i < record->nparams; i++) {
		printf("===== %s %s - %llu =======\n",
		       virDomainGetName(record->dom),
		       record->params[i].field,
		       record->params[i].value.ul);
	}
}

// Populates previousDomainStats with all the stats in
// the  domainStats.
void calculateDomainUsage( domainStats *previousDomainStats,
			   domainStats *currentDomainStats,
			  int domainsCount,
			  long period)
{
	double avgUsage;

	// i represents number of domains
	for (int i = 0; i < domainsCount; i++) {
		currentDomainStats[i].usage =
			calloc(currentDomainStats[i].vcpusCount,
			       sizeof(double));
		avgUsage = 0.0;
		// j represents vcpu number
		for (int j = 0; j < currentDomainStats[i].vcpusCount; j++) {
			currentDomainStats[i].usage[j] =
				findUsage(currentDomainStats[i].vcpus[j] -
				      previousDomainStats[i].vcpus[j],
				      period);
			printf("  - vCPU %d usage: %f%%\n",
			       j,
			       currentDomainStats[i].usage[j]);
			avgUsage += currentDomainStats[i].usage[j];

		}
		currentDomainStats[i].avgUsage =
			avgUsage/currentDomainStats[i].vcpusCount;
	}
	memcpy(previousDomainStats, currentDomainStats,
	       domainsCount * sizeof( domainStats));
}


domainStats createdomainStats(virDomainStatsRecordPtr record)
{
	// One could sample vCPUs here - with vcpu.0.time etc...
	// We know that values are always unsigned long because
	// we just queried for VCPU info
	domainStats ret;
	int vcpusCount = 0;
	int vcpu_number, field_len;
	unsigned long long int *current_vcpus;
	const char *last_four;

	for (int i = 0; i < record->nparams; i++) {
		if (strcmp(record->params[i].field, "vcpu.current") == 0) {
			vcpusCount = record->params[i].value.i;
			current_vcpus = (unsigned long long int *)
				calloc(vcpusCount, sizeof(unsigned long long int));
			check(current_vcpus != NULL,
			      "Could not allocate memory for stats ");
		}
		field_len = strlen(record->params[i].field);
		if (field_len >= 4) {
			last_four = &record->params[i].field[field_len-4];
			if (strcmp(last_four, "time") == 0) {
				vcpu_number = atoi(&record->params[i].field[field_len-6]); // vCPU number
				current_vcpus[vcpu_number] = record->params[i].value.ul;
		}
            }
	}
	ret.domain = record->dom;
	ret.vcpusCount = vcpusCount;
	ret.vcpus = current_vcpus;
	return ret;
error:
	exit(1);
}


void pinPcpus(int nrCpus,
	       domainStats *domainStats, int domainsCount)
{
	int freest;
        virDomainPtr busiestVcpuDomain[nrCpus];
	double freest_usage = 100.0;
	int busiest;
        double busiest_vcpuUsage[nrCpus];
        int busiest_vcpu[nrCpus];
	double busiest_usage = 0.0;
        double cpuUsage[nrCpus]; 
        virVcpuInfoPtr cpuinfo;
        unsigned char *cpumaps;
        size_t cpumaplen;
        int vcpus_per_cpu[nrCpus];
        memset(vcpus_per_cpu, 0, sizeof(int) * nrCpus);
        memset(busiest_vcpuUsage, 0.0, sizeof(double) * nrCpus);
        memset(cpuUsage, 0.0, sizeof(double) * nrCpus);
        memset(busiest_vcpu, 0, sizeof(int) * nrCpus);
        memset(busiestVcpuDomain, 0, sizeof(virDomainPtr) * nrCpus);
        for (int i = 0; i < domainsCount ; i++) {
                cpuinfo = calloc(domainStats[i].vcpusCount,
                                 sizeof(virVcpuInfo));
                cpumaplen = VIR_CPU_MAPLEN(nrCpus);
                cpumaps = calloc(domainStats[i].vcpusCount,
                                 cpumaplen);
                check(virDomainGetVcpus(domainStats[i].domain,
                                        cpuinfo,
                                        domainStats[i].vcpusCount,
                                        cpumaps, cpumaplen) > 0,
                      "Could not retrieve vCpus affinity info");
                for (int j = 0; j < domainStats[i].vcpusCount; j++) {
                         vcpus_per_cpu[cpuinfo[j].cpu] += 1;
                         cpuUsage[cpuinfo[j].cpu] += domainStats[i].usage[j];
                          if (domainStats[i].usage[j]>busiest_vcpuUsage[cpuinfo[j].cpu]) {
                              busiest_vcpuUsage[cpuinfo[j].cpu]= domainStats[i].usage[j];
                              busiest_vcpu[cpuinfo[j].cpu]= j;
                              busiestVcpuDomain[cpuinfo[j].cpu] = domainStats[i].domain;
                        }
                }
        }

        for (int i = 0; i < nrCpus; i++) {
                if (vcpus_per_cpu[i] != 0) {
                        printf("CPU %d - # vCPUs assigned %d - usage %f%%\n",
                               i,
                               vcpus_per_cpu[i],
                               cpuUsage[i]);
                }
        }

	for (int i = 0; i < nrCpus; i++) {
                printf(" \n ===PCPU %d usage: %f " ,i, cpuUsage[i]);
		if (cpuUsage[i] > busiest_usage) {
			busiest_usage = cpuUsage[i];
			busiest = i;
		}
		if (cpuUsage[i] < freest_usage) {
			freest_usage = cpuUsage[i];
			freest = i;
		}
	} 
	if (busiest_usage - freest_usage < 10) {
		printf("should not change pinnings\n");
		printf("Busiest CPU: %d - Freest CPU: %d\n", busiest, freest);
         	printf("Busiest CPU: %f - Freest CPU: %f\n", busiest_usage, freest_usage);
	        return;
	}
	printf("Busiest CPU: %d - Freest CPU: %d\n", busiest, freest);
        printf("Busiest CPU usage: %f - Freest CPU usage : %f\n", busiest_usage, freest_usage);
	printf("Busiest CPU above Usage threshold of %f%%\n", USAGE_THRESHOLD);
	unsigned char freest_map = 0x1 << freest;
      
	printf("%s vCPU %d is one of the busiest. Change Pins \n",
		       virDomainGetName(busiestVcpuDomain[busiest]),
		        busiest_vcpu[busiest]);
                              if (vcpus_per_cpu[busiest] > 1) {
                                 printf(" ==========more than once vcpu associated with pcpu===== ");
		                 virDomainPinVcpu(busiestVcpuDomain[busiest],
				 busiest_vcpu[busiest],
				 &freest_map,
				 cpumaplen);
                              }
         

        free(cpuinfo);
        free(cpumaps);
	return;
error:
	exit(1);

}

