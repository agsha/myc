//
// Created by Sharath Gururaj on 8/14/17.
//

#ifndef MYC_CONFLICTS_H
#define MYC_CONFLICTS_H

#include<sys/socket.h>
int conflict_bind(int socket, const struct sockaddr *address, socklen_t address_len) {
    return bind(socket, address, address_len );
}
#endif //MYC_CONFLICTS_H
