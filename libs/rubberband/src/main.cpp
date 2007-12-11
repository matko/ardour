/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Rubber Band
    An audio time-stretching and pitch-shifting library.
    Copyright 2007 Chris Cannam.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "RubberBandStretcher.h"

#include <iostream>
#include <sndfile.h>
#include <cmath>
#include <sys/time.h>
#include <time.h>
#include "sysutils.h"

#include <getopt.h>

// for import and export of FFTW wisdom
#include <fftw3.h>

using namespace std;
using namespace RubberBand;

#ifdef _WIN32
using RubberBand::gettimeofday;
using RubberBand::usleep;
#endif

int main(int argc, char **argv)
{
    int c;

    double ratio = 1.0;
    double pitchshift = 1.0;
    double frequencyshift = 1.0;
    int debug = 0;
    bool realtime = false;
    bool precise = false;
    int threading = 0;
    bool peaklock = true;
    bool longwin = false;
    bool shortwin = false;
    bool softening = true;
    int crispness = -1;
    bool help = false;
    bool quiet = false;

    bool haveRatio = false;

    enum {
        NoTransients,
        BandLimitedTransients,
        Transients
    } transients = Transients;

    float fthresh0 = -1.f;
    float fthresh1 = -1.f;
    float fthresh2 = -1.f;

    while (1) {
        int optionIndex = 0;

        static struct option longOpts[] = {
            { "help",          0, 0, 'h' },
            { "time",          1, 0, 't' },
            { "tempo",         1, 0, 'T' },
            { "pitch",         1, 0, 'p' },
            { "frequency",     1, 0, 'f' },
            { "crisp",         1, 0, 'c' },
            { "crispness",     1, 0, 'c' },
            { "debug",         1, 0, 'd' },
            { "realtime",      0, 0, 'R' },
            { "precise",       0, 0, 'P' },
            { "no-threads",    0, 0, '0' },
            { "no-transients", 0, 0, '1' },
            { "no-peaklock",   0, 0, '2' },
            { "window-long",   0, 0, '3' },
            { "window-short",  0, 0, '4' },
            { "thresh0",       1, 0, '5' },
            { "thresh1",       1, 0, '6' },
            { "thresh2",       1, 0, '7' },
            { "bl-transients", 0, 0, '8' },
            { "no-softening",  0, 0, '9' },
            { "threads",       0, 0, '@' },
            { "quiet",         0, 0, 'q' },
            { 0, 0, 0 }
        };

        c = getopt_long(argc, argv, "t:p:d:RPc:f:qh", longOpts, &optionIndex);
        if (c == -1) break;

        switch (c) {
        case 'h': help = true; break;
        case 't': ratio *= atof(optarg); haveRatio = true; break;
        case 'T': { double m = atof(optarg); if (m != 0.0) ratio /= m; }; haveRatio = true; break;
        case 'p': pitchshift = atof(optarg); haveRatio = true; break;
        case 'f': frequencyshift = atof(optarg); haveRatio = true; break;
        case 'd': debug = atoi(optarg); break;
        case 'R': realtime = true; break;
        case 'P': precise = true; break;
        case '0': threading = 1; break;
        case '@': threading = 2; break;
        case '1': transients = NoTransients; break;
        case '2': peaklock = false; break;
        case '3': longwin = true; break;
        case '4': shortwin = true; break;
        case '5': fthresh0 = atof(optarg); break;
        case '6': fthresh1 = atof(optarg); break;
        case '7': fthresh2 = atof(optarg); break;
        case '8': transients = BandLimitedTransients; break;
        case '9': softening = false; break;
        case 'c': crispness = atoi(optarg); break;
        case 'q': quiet = true; break;
        default:  help = true; break;
        }
    }

    if (help || !haveRatio || optind + 2 != argc) {
        cerr << endl;
	cerr << "Rubber Band" << endl;
        cerr << "An audio time-stretching and pitch-shifting library and utility program." << endl;
	cerr << "Copyright 2007 Chris Cannam.  Distributed under the GNU General Public License." << endl;
        cerr << endl;
	cerr << "   Usage: " << argv[0] << " [options] <infile.wav> <outfile.wav>" << endl;
        cerr << endl;
        cerr << "You must specify at least one of the following time and pitch ratio options." << endl;
        cerr << endl;
        cerr << "  -t<X>, --time <X>       Stretch to X times original duration, or" << endl;
        cerr << "  -T<X>, --tempo <X>      Change tempo by multiple X (equivalent to --time 1/X)" << endl;
        cerr << endl;
        cerr << "  -p<X>, --pitch <X>      Raise pitch by X semitones, or" << endl;
        cerr << "  -f<X>, --frequency <X>  Change frequency by multiple X" << endl;
        cerr << endl;
        cerr << "The following option provides a simple way to adjust the sound.  See below" << endl;
        cerr << "for more details." << endl;
        cerr << endl;
        cerr << "  -c<N>, --crisp <N>      Crispness (N = 0,1,2,3,4,5); default 4 (see below)" << endl;
        cerr << endl;
        cerr << "The remaining options fine-tune the processing mode and stretch algorithm." << endl;
        cerr << "These are mostly included for test purposes; the default settings and standard" << endl;
        cerr << "crispness parameter are intended to provide the best sounding set of options" << endl;
        cerr << "for most situations." << endl;
        cerr << endl;
        cerr << "  -P,    --precise        Aim for minimal time distortion (implied by -R)" << endl;
        cerr << "  -R,    --realtime       Select realtime mode (implies -P --no-threads)" << endl;
        cerr << "         --no-threads     No extra threads regardless of CPU and channel count" << endl;
        cerr << "         --threads        Assume multi-CPU even if only one CPU is identified" << endl;
        cerr << "         --no-transients  Disable phase resynchronisation at transients" << endl;
        cerr << "         --bl-transients  Band-limit phase resync to extreme frequencies" << endl;
        cerr << "         --no-peaklock    Disable phase locking to peak frequencies" << endl;
        cerr << "         --no-softening   Disable large-ratio softening of phase locking" << endl;
        cerr << "         --window-long    Use longer processing window (actual size may vary)" << endl;
        cerr << "         --window-short   Use shorter processing window" << endl;
        cerr << "         --thresh<N> <F>  Set internal freq threshold N (N = 0,1,2) to F Hz" << endl;
        cerr << endl;
        cerr << "  -d<N>, --debug <N>      Select debug level (N = 0,1,2,3); default 0, full 3" << endl;
        cerr << "                          (N.B. debug level 3 includes audible ticks in output)" << endl;
        cerr << "  -q,    --quiet          Suppress progress output" << endl;
        cerr << endl;
        cerr << "  -h,    --help           Show this help" << endl;
        cerr << endl;
        cerr << "\"Crispness\" levels:" << endl;
        cerr << "  -c 0   equivalent to --no-transients --no-peaklock --window-long" << endl;
        cerr << "  -c 1   equivalent to --no-transients --no-peaklock" << endl;
        cerr << "  -c 2   equivalent to --no-transients" << endl;
        cerr << "  -c 3   equivalent to --bl-transients" << endl;
        cerr << "  -c 4   default processing options" << endl;
        cerr << "  -c 5   equivalent to --no-peaklock --window-short (may be suitable for drums)" << endl;
        cerr << endl;
	return 2;
    }

    switch (crispness) {
    case -1: crispness = 4; break;
    case 0: transients = NoTransients; peaklock = false; longwin = true; shortwin = false; break;
    case 1: transients = NoTransients; peaklock = false; longwin = false; shortwin = false; break;
    case 2: transients = NoTransients; peaklock = true; longwin = false; shortwin = false; break;
    case 3: transients = BandLimitedTransients; peaklock = true; longwin = false; shortwin = false; break;
    case 4: transients = Transients; peaklock = true; longwin = false; shortwin = false; break;
    case 5: transients = Transients; peaklock = false; longwin = false; shortwin = true; break;
    };

    if (!quiet) {
        cerr << "Using crispness level: " << crispness << " (";
        switch (crispness) {
        case 0: cerr << "Mushy"; break;
        case 1: cerr << "Smooth"; break;
        case 2: cerr << "Balanced multitimbral mixture"; break;
        case 3: cerr << "Unpitched percussion with stable notes"; break;
        case 4: cerr << "Crisp monophonic instrumental"; break;
        case 5: cerr << "Unpitched solo percussion"; break;
        }
        cerr << ")" << endl;
    }

    char *fileName = strdup(argv[optind++]);
    char *fileNameOut = strdup(argv[optind++]);
    
    SNDFILE *sndfile;
    SNDFILE *sndfileOut;
    SF_INFO sfinfo;
    SF_INFO sfinfoOut;
    memset(&sfinfo, 0, sizeof(SF_INFO));

    sndfile = sf_open(fileName, SFM_READ, &sfinfo);
    if (!sndfile) {
	cerr << "ERROR: Failed to open input file \"" << fileName << "\": "
	     << sf_strerror(sndfile) << endl;
	return 1;
    }

    sfinfoOut.channels = sfinfo.channels;
    sfinfoOut.format = sfinfo.format;
    sfinfoOut.frames = int(sfinfo.frames * ratio + 0.1);
    sfinfoOut.samplerate = sfinfo.samplerate;
    sfinfoOut.sections = sfinfo.sections;
    sfinfoOut.seekable = sfinfo.seekable;

    sndfileOut = sf_open(fileNameOut, SFM_WRITE, &sfinfoOut) ;
    if (!sndfileOut) {
	cerr << "ERROR: Failed to open output file \"" << fileName << "\" for writing: "
	     << sf_strerror(sndfile) << endl;
	return 1;
    }
    
    int ibs = 1024;
    size_t channels = sfinfo.channels;

    RubberBandStretcher::Options options = 0;
    if (realtime)    options |= RubberBandStretcher::OptionProcessRealTime;
    if (precise)     options |= RubberBandStretcher::OptionStretchPrecise;
    if (!peaklock)   options |= RubberBandStretcher::OptionPhaseIndependent;
    if (!softening)  options |= RubberBandStretcher::OptionPhasePeakLocked;
    if (longwin)     options |= RubberBandStretcher::OptionWindowLong;
    if (shortwin)    options |= RubberBandStretcher::OptionWindowShort;

    switch (threading) {
    case 0:
        options |= RubberBandStretcher::OptionThreadingAuto;
        break;
    case 1:
        options |= RubberBandStretcher::OptionThreadingNever;
        break;
    case 2:
        options |= RubberBandStretcher::OptionThreadingAlways;
        break;
    }

    switch (transients) {
    case NoTransients:
        options |= RubberBandStretcher::OptionTransientsSmooth;
        break;
    case BandLimitedTransients:
        options |= RubberBandStretcher::OptionTransientsMixed;
        break;
    case Transients:
        options |= RubberBandStretcher::OptionTransientsCrisp;
        break;
    }

    if (pitchshift != 1.0) {
        frequencyshift *= pow(2.0, pitchshift / 12);
    }

#ifdef _WIN32
    RubberBand::
#endif
    timeval tv;
    (void)gettimeofday(&tv, 0);

    RubberBandStretcher::setDefaultDebugLevel(debug);

    RubberBandStretcher ts(sfinfo.samplerate, channels, options,
                           ratio, frequencyshift);

    ts.setExpectedInputDuration(sfinfo.frames);

    float *fbuf = new float[channels * ibs];
    float **ibuf = new float *[channels];
    for (size_t i = 0; i < channels; ++i) ibuf[i] = new float[ibs];

    int frame = 0;
    int percent = 0;

    sf_seek(sndfile, 0, SEEK_SET);

    if (!realtime) {

        if (!quiet) {
            cerr << "Pass 1: Studying..." << endl;
        }

        while (frame < sfinfo.frames) {

            int count = -1;

            if ((count = sf_readf_float(sndfile, fbuf, ibs)) <= 0) break;
        
            for (size_t c = 0; c < channels; ++c) {
                for (int i = 0; i < count; ++i) {
                    float value = fbuf[i * channels + c];
                    ibuf[c][i] = value;
                }
            }

            bool final = (frame + ibs >= sfinfo.frames);

            ts.study(ibuf, count, final);

            int p = int((double(frame) * 100.0) / sfinfo.frames);
            if (p > percent || frame == 0) {
                percent = p;
                if (!quiet) {
                    cerr << "\r" << percent << "% ";
                }
            }

            frame += ibs;
        }

        if (!quiet) {
            cerr << "\rCalculating profile..." << endl;
        }

        sf_seek(sndfile, 0, SEEK_SET);
    }

    frame = 0;
    percent = 0;
    
    size_t countIn = 0, countOut = 0;

    while (frame < sfinfo.frames) {

        int count = -1;

	if ((count = sf_readf_float(sndfile, fbuf, ibs)) < 0) break;
        
        countIn += count;

        for (size_t c = 0; c < channels; ++c) {
            for (int i = 0; i < count; ++i) {
                float value = fbuf[i * channels + c];
                ibuf[c][i] = value;
            }
        }

        bool final = (frame + ibs >= sfinfo.frames);

        ts.process(ibuf, count, final);

        int avail = ts.available();
        if (debug > 1) cerr << "available = " << avail << endl;

        if (avail > 0) {
            float **obf = new float *[channels];
            for (size_t i = 0; i < channels; ++i) {
                obf[i] = new float[avail];
            }
            ts.retrieve(obf, avail);
            countOut += avail;
            float *fobf = new float[channels * avail];
            for (size_t c = 0; c < channels; ++c) {
                for (int i = 0; i < avail; ++i) {
                    float value = obf[c][i];
                    if (value > 1.f) value = 1.f;
                    if (value < -1.f) value = -1.f;
                    fobf[i * channels + c] = value;
                }
            }
//            cout << "fobf mean: ";
//    double d = 0;
//    for (int i = 0; i < avail; ++i) {
//        d += fobf[i];
//    }
//    d /= avail;
//    cout << d << endl;
            sf_writef_float(sndfileOut, fobf, avail);
            delete[] fobf;
            for (size_t i = 0; i < channels; ++i) {
                delete[] obf[i];
            }
            delete[] obf;
        }

        if (frame == 0 && !realtime && !quiet) {
            cerr << "Pass 2: Processing..." << endl;
        }

	int p = int((double(frame) * 100.0) / sfinfo.frames);
	if (p > percent || frame == 0) {
	    percent = p;
            if (!quiet) {
                cerr << "\r" << percent << "% ";
            }
	}

        frame += ibs;
    }

    if (!quiet) {
        cerr << "\r    " << endl;
    }
    int avail;

    while ((avail = ts.available()) >= 0) {

        if (debug > 1) {
            cerr << "(completing) available = " << avail << endl;
        }

        if (avail > 0) {
            float **obf = new float *[channels];
            for (size_t i = 0; i < channels; ++i) {
                obf[i] = new float[avail];
            }
            ts.retrieve(obf, avail);
            countOut += avail;
            float *fobf = new float[channels * avail];
            for (size_t c = 0; c < channels; ++c) {
                for (int i = 0; i < avail; ++i) {
                    float value = obf[c][i];
                    if (value > 1.f) value = 1.f;
                    if (value < -1.f) value = -1.f;
                    fobf[i * channels + c] = value;
                }
            }

            sf_writef_float(sndfileOut, fobf, avail);
            delete[] fobf;
            for (size_t i = 0; i < channels; ++i) {
                delete[] obf[i];
            }
            delete[] obf;
        } else {
            usleep(10000);
        }
    }

    sf_close(sndfile);
    sf_close(sndfileOut);

    if (!quiet) {

        cerr << "in: " << countIn << ", out: " << countOut << ", ratio: " << float(countOut)/float(countIn) << ", ideal output: " << lrint(countIn * ratio) << ", error: " << abs(lrint(countIn * ratio) - int(countOut)) << endl;

#ifdef _WIN32
        RubberBand::
#endif
        timeval etv;
        (void)gettimeofday(&etv, 0);
        
        etv.tv_sec -= tv.tv_sec;
        if (etv.tv_usec < tv.tv_usec) {
            etv.tv_usec += 1000000;
            etv.tv_sec -= 1;
        }
        etv.tv_usec -= tv.tv_usec;
        
        double sec = double(etv.tv_sec) + (double(etv.tv_usec) / 1000000.0);
        cerr << "elapsed time: " << sec << " sec, in frames/sec: " << countIn/sec << ", out frames/sec: " << countOut/sec << endl;
    }

    return 0;
}


