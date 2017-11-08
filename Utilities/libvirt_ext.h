#ifndef __libvirt_ext__
#define __libvirt_ext__

#include <libvirt/libvirt.h>


/* Structure to store  an array of domain list and its total count */
typedef struct domainList {
     virDomainPtr *domains;
     int totalCount;
} domainList;


/* makeLocalConnection() :: This method creates a local connection with the qemu libvirt and returns and pointer to it. */

virConnectPtr makeLocalConnection(void);

/* getDomainsList :: This Method Accepts a connection to libvirt qemu and filter flags and returns a list of domains for the given flag conditions */

domainList  getDomainsList(virConnectPtr conn, unsigned int flags);


/* getDomains ::  This Method accpets a connection pointer to libvirt qemu and isActiveFlag (0/1) to specifiy if you want to access active ot inactive domains and returns a domainList */

domainList  getDomains(virConnectPtr conn, int isActiveFlag);

#endif
