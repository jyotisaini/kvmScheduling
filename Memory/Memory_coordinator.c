#include<stdio.h>
#include<stdlib.h>
#include<../Utilities/libvirt_ext.c>
#include<unistd.h>

#define INT_MAX 2147483647

#define activeON 1
#define activeOFF 0

typedef struct DomainMemory {
        virDomainPtr domain;
        long memory;
} DomainMemory;

static const double STARVATION_MEMORY_THRESHOLD = 20;
static const double WASTE_MEMORY_THRESHOLD = 50;
static const unsigned long long int MIN_MEMORY_FOR_DOMAIN = 50*1024;
static const double HYPERVISOR_UNUSED_MEMORY_THRESHOLD=5;

enum domainStatus {BALANCED, STARVING, WASTING};

typedef struct DomainMemoryStats {
	unsigned long long totalMemory;
	unsigned long long unusedMemory;
	float percentUnusedMemory;
	virDomainPtr domain;	
} DomainMemoryStats;

typedef struct HostMemoryStats{
	unsigned long long freeMemory;
	unsigned long long totalMemory;
	float percentUnusedMemory;
}HostMemoryStats;

typedef struct DomainMemoryRateStats
{
	unsigned long long rateOfUsage;
	DomainMemoryStats domainMemoryStats;
	enum domainStatus status;
}DomainMemoryRateStats;

void assignMemory(virConnectPtr conn, domainList domainLists);
DomainMemoryStats *populateDomainMemoryStats(domainList list);
HostMemoryStats getVirHostFreeMemoryStats(virConnectPtr local);
void getVirDomainMemoryStats(domainList domainLists);
void printDomainMemoryStats(DomainMemoryStats *stats, int domainsCount);
DomainMemoryRateStats popuateDomainMemoryRateStats(DomainMemoryStats old, DomainMemoryStats new,int period);


char * TagValueMap(int tag) {
     char *value;
     switch(tag)
     { 
        case VIR_DOMAIN_MEMORY_STAT_NR:
                value = "NR";
                break;
        case VIR_DOMAIN_MEMORY_STAT_RSS:
                value = "RSS (Resident Set Size)";
                break;
        case VIR_DOMAIN_MEMORY_STAT_SWAP_IN:
		value = "SWAP IN";
		break;
	case VIR_DOMAIN_MEMORY_STAT_SWAP_OUT:
		value = "SWAP OUT";
		break;
        case VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON:
                value = "CURRENT BALLOON";
                break;
	case VIR_DOMAIN_MEMORY_STAT_MAJOR_FAULT:
		value = "MAJOR FAULT";
		break;
	case VIR_DOMAIN_MEMORY_STAT_MINOR_FAULT:
		value = "MINOR FAULT";
		break;
	case VIR_DOMAIN_MEMORY_STAT_UNUSED:
		value = "UNUSED";
		break;
	case VIR_DOMAIN_MEMORY_STAT_AVAILABLE:
		value = "AVAILABLE";
		break;
   } 
  return value;
  }


/* Main starts here */
int main(int argc, char **argv) {

   check(argc == 2, " Error : Please enter only one argument i.e time interval after whick memory coordinator will trigger the procss");

   printf("local connecting to hypervisor \n");
   virConnectPtr localConnection =  makeLocalConnection();
   domainList domains = getDomains(localConnection, activeON);
   int isStarvingDomainPresent = 0;	
   int isWastingDomainPresent = 0;
   int period = atoi(argv[1]);
   DomainMemoryStats *oldDomainStats = populateDomainMemoryStats(domains);
  // printf("popuating old status=== >\n");
   printDomainMemoryStats(oldDomainStats, domains.totalCount);
   sleep(period);
  // Loop until no active domains available or program halts 
  while (domains.totalCount > 0) {
   
   // get free memory of Host
   HostMemoryStats hostMemoryStats =  getVirHostFreeMemoryStats(localConnection);
   printf("\n hostFreeMemroy = %llu MB",  hostMemoryStats.freeMemory/1024);
   printf("\n HostTotalMemory = %llu MB",  hostMemoryStats.totalMemory/1024);
 
     DomainMemoryStats *newDomainStats = populateDomainMemoryStats(domains);
     printDomainMemoryStats(newDomainStats, domains.totalCount);
     double minStarvingUsage = INT_MAX;
     unsigned long long starvingRate;
     unsigned long long maxUnusedMemory = 0 , maxTotalMemory =0;
     DomainMemoryStats starvingDomain,maxMemoryDomain;
     for( int i=0; i< domains.totalCount; i++) {
         DomainMemoryRateStats currentDomainRateStats = popuateDomainMemoryRateStats(oldDomainStats[i], newDomainStats[i],period);
 /* for each domain check if its most starving domain  */
    	if(currentDomainRateStats.status == 1 && minStarvingUsage > newDomainStats[i].percentUnusedMemory ) {
    	    starvingDomain  = newDomainStats[i];
    	    minStarvingUsage = newDomainStats[i].percentUnusedMemory;
    	    starvingRate = currentDomainRateStats.rateOfUsage;
    	    isStarvingDomainPresent = 1;
        }

/* for each domain check if its most wasting domain */
        else if(currentDomainRateStats.status == 2 && maxUnusedMemory < newDomainStats[i].unusedMemory){
        	maxMemoryDomain = newDomainStats[i];
        	maxUnusedMemory = newDomainStats[i].unusedMemory;
        	maxTotalMemory = newDomainStats[i].totalMemory;
            isWastingDomainPresent = 1;
        }
    }
 /*If both staring and wasting domains are present take memory from wasting domain and give it to starving domain. */
     if(isStarvingDomainPresent && isWastingDomainPresent){
     	if(maxTotalMemory - starvingRate*period*2 < MIN_MEMORY_FOR_DOMAIN  ||
     	  starvingDomain.totalMemory + starvingRate*period*2 > virDomainGetMaxMemory(starvingDomain.domain)  ) {
	        printf("Min memory for a domain can not be leass than 50MB and A Domin can not have more than MaxDomainMemory. Ask HyperVisor for Memory" ); 
	      }
	      else
	      {       
	       printf("wasting Domain : %s Unused Memory : %llu, Starving domain : %s, ununsed Memory : %llu)",
           virDomainGetName(maxMemoryDomain.domain), maxMemoryDomain.unusedMemory/1024,
           virDomainGetName(starvingDomain.domain), starvingDomain.unusedMemory/1024) ;   
           printf("Removing memory %llu MB from wasted domain %s and adding to starving domain %s \n",
           starvingRate*period*2/1024, virDomainGetName(maxMemoryDomain.domain), 
           virDomainGetName(starvingDomain.domain));
	       virDomainSetMemory(maxMemoryDomain.domain,
					   maxTotalMemory - starvingRate*period*2);
	       virDomainSetMemory(starvingDomain.domain,
				   starvingDomain.totalMemory + starvingRate*period*2);
	      }
	 } 
/*  If only starving domain is present take memory from hypervisor if hypervisor has enough memory*/
	else if(isStarvingDomainPresent) {
		 printf("Starving domain : %s, ununsed Memory : %llu)",
         virDomainGetName(starvingDomain.domain), starvingDomain.unusedMemory/1024) ;
         int percentageMemory = (int)((hostMemoryStats.totalMemory- starvingRate*period*2)*100.0/(hostMemoryStats.totalMemory+0.5));
		   if(percentageMemory < HYPERVISOR_UNUSED_MEMORY_THRESHOLD ) {
		   		printf(" HyperVisor Doesnot have enough memory to provide. percentage usage :%d", percentageMemory);
		 }
		else 
			if(starvingDomain.totalMemory + starvingRate*period*2 > virDomainGetMaxMemory(starvingDomain.domain) ) {
				printf("Can not Assign more memory than Max Domain memory on active Domain");
            /*kill process */
		}
		else 
		{
			printf("Adding memory %llu MB to starved domain %s from hypervisor \n", 
		 		starvingRate*period*2/1024,
			       virDomainGetName(starvingDomain.domain));
			virDomainSetMemory(starvingDomain.domain,
					   starvingDomain.totalMemory + starvingRate*period*2);
		}

	} 
/* if wasting domain present, return memory to hypervisor */
	else if (isWastingDomainPresent) {
		   if( maxTotalMemory - maxMemoryDomain.unusedMemory/2 < MIN_MEMORY_FOR_DOMAIN ) {
			printf("Min memory for a domain can not be less than 50MB. Not Returning to Hypervisor");
		}

		else 
		{
		    printf("Returning memory %llu MB from Domain : %s back to hypervisor\n", 
			maxMemoryDomain.unusedMemory/2/1024,
		    virDomainGetName(maxMemoryDomain.domain));
		    virDomainSetMemory(maxMemoryDomain.domain,
				   maxTotalMemory - maxMemoryDomain.unusedMemory/2);
	    }
	}

	sleep(period);
	isStarvingDomainPresent = 0;
	isWastingDomainPresent = 0;
	memcpy(oldDomainStats, newDomainStats,
			       domains.totalCount * sizeof(DomainMemoryStats));
  }
  printf("No active domains - closing.");
  printf("DONE\n");
  virConnectClose(localConnection);
  free(localConnection);
return 0;
   error:
return 1;
}

void getVirDomainMemoryStats(domainList domainLists) {
    printf("%d Memory Stats supported by the hypervisor", VIR_DOMAIN_MEMORY_STAT_NR);
    printf("========================");
   
    for(int i=0 ; i< domainLists.totalCount; i++)
    {
      virDomainMemoryStatStruct memoryStatInfo[VIR_DOMAIN_MEMORY_STAT_NR];
      memset(memoryStatInfo,
             0,sizeof(virDomainMemoryStatStruct)*VIR_DOMAIN_MEMORY_STAT_NR);
      virDomainMemoryStats(domainLists.domains[i],
                           (virDomainMemoryStatPtr)&memoryStatInfo,
                           VIR_DOMAIN_MEMORY_STAT_NR,0);
           
      for(int i=0; i< VIR_DOMAIN_MEMORY_STAT_NR; i++)
      {
         printf("%s - tag -> %s | val -> %lld MB\n",
                virDomainGetName(domainLists.domains[i]),
                TagValueMap(memoryStatInfo[i].tag),memoryStatInfo[i].val/1024);
      }
    }
}

HostMemoryStats getVirHostFreeMemoryStats(virConnectPtr con) {
    int nParams = 0;
    HostMemoryStats hostMemoryStats;
	virNodeMemoryStatsPtr memoryStats = malloc(sizeof(virNodeMemoryStats));
	if (virNodeGetMemoryStats(con,
				  VIR_NODE_MEMORY_STATS_ALL_CELLS,
				  NULL,
				  &nParams,
				  0) == 0 && nParams != 0) {
		memoryStats = malloc(sizeof(virNodeMemoryStats) * nParams);
		memset(memoryStats, 0, sizeof(virNodeMemoryStats) * nParams);
		virNodeGetMemoryStats(con,
				      VIR_NODE_MEMORY_STATS_ALL_CELLS,
				      memoryStats,
				      &nParams,
				      0);
	}
     for (int i = 0; i < nParams; i++) {
     	//printf("\n======host  %s" ,memoryStats[i].field);
                   if(strcmp(memoryStats[i].field,"free") ==0) {
                   	hostMemoryStats.freeMemory = memoryStats[i].value;
                    }
                    if(strcmp(memoryStats[i].field,"total") ==0) {
                   	hostMemoryStats.totalMemory = memoryStats[i].value;
                    }
                    
         }
         hostMemoryStats.percentUnusedMemory = (hostMemoryStats.freeMemory/hostMemoryStats.totalMemory)*100;
         free(memoryStats);
         return hostMemoryStats;
}

DomainMemoryStats *populateDomainMemoryStats(domainList list){
	 DomainMemoryStats *domainMemoryStats =  malloc(sizeof(DomainMemoryStats)*list.totalCount);
	for (int i = 0; i < list.totalCount; i++) {
		virDomainMemoryStatStruct memstatsInfo[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int nrStats;
		unsigned int flags = VIR_DOMAIN_AFFECT_CURRENT;
		unsigned int enabledPeriod;	

                // Dynamically change the domain memory balloon driver statistics collection period
		//printf("total count of doamins %d", list.totalCount);
		//printf("inside populateDomainMemoryStats %s ", virDomainGetName(list.domains[i]));
		enabledPeriod = virDomainSetMemoryStatsPeriod(list.domains[i],1,flags);
		check(enabledPeriod >= 0, "ERROR: Could not change balloon collecting period");
	    sleep(1);
		nrStats = virDomainMemoryStats(list.domains[i], memstatsInfo, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		check(nrStats != -1,
		      "ERROR: Could not collect memory stats for domain %s",
		      virDomainGetName(list.domains[i]));
		for(int j=0;j< VIR_DOMAIN_MEMORY_STAT_NR ;j++) { 
		  /* debugging purpose
		        printf("%s : %llu MB %d \n",
		       virDomainGetName(list.domains[i]),
		       (memstatsInfo[j].val)/1024, memstatsInfo[j].tag);*/

            if(memstatsInfo[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
            	 domainMemoryStats[i].unusedMemory = memstatsInfo[j].val;
            if(memstatsInfo[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)	
		         domainMemoryStats[i].totalMemory = memstatsInfo[j].val;
            if(memstatsInfo[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
                 domainMemoryStats[i].unusedMemory = memstatsInfo[j].val;

            domainMemoryStats[i].percentUnusedMemory = ((double) domainMemoryStats[i].unusedMemory/domainMemoryStats[i].totalMemory )*100;
        }
        domainMemoryStats[i].domain = list.domains[i];
    }
    return domainMemoryStats;

error:
	exit(1);
}

void assignMemory(virConnectPtr conn, domainList domainLists) {
       HostMemoryStats hostMemoryStats = getVirHostFreeMemoryStats(conn);
       unsigned long assignMemory = hostMemoryStats.freeMemory/2;
       for(int i=0 ; i< domainLists.totalCount; i++) {
       if(domainLists.totalCount!=0) {
             virDomainSetMemory(domainLists.domains[i], assignMemory/domainLists.totalCount);
       } 
       printf( "=======Initial Memory Assigned to %s = %lu MB \n " ,virDomainGetName(domainLists.domains[i]) , assignMemory/domainLists.totalCount/1024);

       } 
}


void printDomainMemoryStats(DomainMemoryStats *stats, int domainsCount) {
	for(int i=0; i< domainsCount; i++) {
	DomainMemoryStats currentDomain = stats[i];
	printf("\n======================================================");
	printf("\n Unused Memeory for domain %s -==> %llu MB", virDomainGetName(currentDomain.domain), currentDomain.unusedMemory/1024);
	//printf("\n Total Memeory for domain %s -==> %llu MB", virDomainGetName(currentDomain.domain), currentDomain.totalMemory/1024);
	printf("\n Percentage Unused Memeory for domain %s -==> %f", virDomainGetName(currentDomain.domain), currentDomain.percentUnusedMemory);
	//printf("\n status  for domain %s -==> %d", virDomainGetName(currentDomain.domain), currentDomain.status);
	}
   
}

DomainMemoryRateStats popuateDomainMemoryRateStats(DomainMemoryStats oldDomainStats, DomainMemoryStats newDomainStats,int period) {
	DomainMemoryRateStats domainRateStats;
	domainRateStats.rateOfUsage = (oldDomainStats.unusedMemory-newDomainStats.unusedMemory)/period;
	domainRateStats.domainMemoryStats =  newDomainStats;
	//printf("\n=======================");
	//printf(" Domain :%s Rate of Usage :  %llu", virDomainGetName(newDomainStats.domain), domainRateStats.rateOfUsage );

	if(domainRateStats.rateOfUsage > 1.0 && newDomainStats.percentUnusedMemory > STARVATION_MEMORY_THRESHOLD) 
		  domainRateStats.status = BALANCED;
	else if (domainRateStats.rateOfUsage > 1.0 && newDomainStats.percentUnusedMemory <= STARVATION_MEMORY_THRESHOLD)
		 domainRateStats.status = STARVING;
    else if (domainRateStats.rateOfUsage <1 && newDomainStats.percentUnusedMemory > WASTE_MEMORY_THRESHOLD) 
           domainRateStats.status = WASTING;
    else if(domainRateStats.rateOfUsage <1 && newDomainStats.percentUnusedMemory <= WASTE_MEMORY_THRESHOLD )  
			domainRateStats.status = BALANCED;
	 
return domainRateStats;
}
