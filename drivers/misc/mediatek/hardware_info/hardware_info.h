
#ifndef _HARDWARE_INFO_H_
#define _HARDWARE_INFO_H_

struct hardware_info{
	unsigned char chip[32];
	unsigned char vendor[32];
	unsigned char id[32];
	unsigned char more[64];
};

#endif /* _HARDWARE_INFO_H_ */
