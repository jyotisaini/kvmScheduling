#include<stdlib.h>
#include<../Utilities/logger.h>
#include<../Utilities/libvirt_ext.h>


virConnectPtr makeLocalConnection() {
   virConnectPtr conn ;
   conn =  virConnectOpen("qemu:///system");
   if(conn == 0){
   printf("can not connect \n");
   exit(-1);
  }
  return conn;

}

domainList getDomains(virConnectPtr conn, int isActive) {

     unsigned int flags;
     if(isActive)
       flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
     else
       flags =  VIR_CONNECT_LIST_DOMAINS_INACTIVE;
     return getDomainsList(conn, flags);
}


domainList getDomainsList(virConnectPtr conn, unsigned int flags) {
   domainList *domainLists;
   virDomainPtr *domains;
   int totalDomains;

    totalDomains = virConnectListAllDomains(conn, &domains, flags) ;
    if(totalDomains <=0) {
      printf("No domains found");
      exit(1);
    }
    else
      printf(" %d Domains found===========\n", totalDomains);

    for(int i=0; i< totalDomains; i++)
      printf( " %s \n", virDomainGetName(domains[i]));
    domainLists = malloc(sizeof(domainList));
    domainLists->totalCount = totalDomains;
    domainLists->domains = domains;
    return *domainLists;
}

