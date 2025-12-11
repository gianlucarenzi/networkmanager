#ifndef _ETHERRORS_INCLUDED_
#define _ETHERRORS_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ETHNOERR        =  0,
    ETHSTATEUP      =  1,
    ETHSTATEDOWN    = -1,
    ETHEMPTYCMD     = -2,
    ETHPOPENERR     = -3,
    ETHFREADERR     = -4,
    ETHDEVICEERR    = -5,
    ETHBADCONFERR   = -6,
    ETHNTPSERVERERR = -7,
    ETHLINKERR      = -8,
    ETHCONFIGBUSY   = -9,
};

#ifdef __cplusplus
}
#endif

#endif
