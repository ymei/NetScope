#ifndef __COMMON_H__
#define __COMMON_H__

#define HDF5IO(name) hdf5io_ ## name

#define SCOPE_NCH 4
#define SCOPE_MEM_LENGTH_MAX 12500000 /* DPO5054 default, 12.5M points maximum */

struct waveform_attribute 
{
    unsigned int chMask;
    size_t nPt; /* number of points in each event */
    size_t nFrames; /* number of Fast Frames in each event, 0 means off */
    double dt;
    double t0;
    double ymult[SCOPE_NCH];
    double yoff[SCOPE_NCH];
    double yzero[SCOPE_NCH];
};

#endif /* __COMMON_H__ */
