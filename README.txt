NAME
        NetScope, dpo5054

SYNOPSIS
        dpo5054 host port outfile.h5 chMask nEvents [nWaveformsPerChunk]
                    (4000)           (0x0f)         [default = 100]

DESCRIPTION

    NetScope connects to an oscilloscope with running `socket server'
at host:port via network (TCP), reads back the waveforms and saves to
an HDF5 file.  Currently Tektronix DPO5054 is tested and supported.
With slight modification this program could be easily adapted to work
with DPO5000 and DPO7000 series, as well as Agilent scopes with VISA
support and `socket server' running.

    chMask, works as a bit mask, indicates which channels should be
turned on and recorded.  Before starting the acquisition, the desired
sampling speed and recording length should be set on the scope.  While
data taking is in progress, no scope settings should be changed.

    Waveforms are stored in the HDF5 file in 2D arrays.  To minimize
the HDF5 structural overhead, more than 1 waveforms are stored in a
chunk (the default is 100 waveforms per chunk).  The hdf5io routines
make this detail transparent.

    `wavedump' reads HDF5 files and dump the data in columns to
stdout.  It can be used to feed gnuplot in order to have a quick view
of the waveforms.

KNOWN BUGS

    Tektronix tech-support confirms that, while using the
`curvestream?'  command, the network read-back could be faster than
the speed the internal buffer is filled, resulting in socket server
hang-up.  Therefore, in this implementation, a `curve?' --- read-back
loop is used, which achieves lower data rate but is stable.

RANDOM NOTES

    Set `Horizontal Mode' to be `Constant Sample Rate' in the scope,
then one can specify the Sample Rate and Scale (total length) in order
to fully utilize the 12.5M sampling points.

    When the direct socket (TCP) server screws up, use vxi11_cmd to
connect and send a command like *IDN? or DCL, then it recovers.

    After setsockopt IPPROTO_TCP, TCP_NODELAY, it is much less likely
that the socket (TCP) server gets screwed up.

    In FastAcq mode, curve? won't work.

    Fast Frame mode is a great tool to take short period waveforms but
a lot of them with little dead time.  For instance it is great for
taking self-triggered PMT pulses.

    In Fast Frame mode, at 1000 frame size, it seems # of frames in
each event should not be greater than 400.
 2000           , 300
10000 frame size, 300 frames/event
50000           , 200
