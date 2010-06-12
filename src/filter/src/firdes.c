/*
 * Copyright (c) 2007, 2009 Joseph Gaeddert
 * Copyright (c) 2007, 2009 Virginia Polytechnic Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// Finite impulse response filter design
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "liquid.internal.h"

// esimate required filter length given
//   _b    : transition bandwidth (0 < b < 0.5)
//   _slsl : sidelobe suppression level [dB]
unsigned int estimate_req_filter_len(float _b, float _slsl)
{
    if (_b > 0.5f || _b <= 0.0f) {
        printf("error: estimate_req_filter_len(), invalid bandwidth : %f\n", _b);
        exit(0);
    }

    if (_slsl <= 0.0f) {
        printf("error: estimate_req_filter_len(), invalid sidelobe level : %f\n", _slsl);
        exit(0);
    }

    unsigned int h_len;
    if (_slsl < 8) {
        h_len = 2;
    } else {
        h_len = (unsigned int) lroundf((_slsl-8)/(14*_b));
    }
    
    return h_len;
}


// returns the Kaiser window beta factor : sidelobe suppression level
float kaiser_beta_slsl(float _slsl)
{
    // from:
    //  P.P. Vaidyanathan, "Multirate Systems and Filter Banks
    _slsl = fabsf(_slsl);
    float beta;
    if (_slsl > 50.0f)
        beta = 0.1102f*(_slsl - 8.7f);
    else if (_slsl > 21.0f)
        beta = 0.5842*powf(_slsl - 21, 0.4f) + 0.07886f*(_slsl - 21);
    else
        beta = 0.0f;

    return beta;
}

// Design FIR using kaiser window
//  _n      : filter length
//  _fc     : cutoff frequency
//  _slsl   : sidelobe suppression level (dB attenuation)
//  _mu     : fractional sample offset [-0.5,0.5]
//  _h      : output coefficient buffer
void fir_kaiser_window(unsigned int _n,
                       float _fc,
                       float _slsl,
                       float _mu,
                       float *_h)
{
    // validate inputs
    if (_mu < -0.5f || _mu > 0.5f) {
        printf("error: fir_kaiser_window(), _mu (%12.4e) out of range [-0.5,0.5]\n", _mu);
        exit(0);
    } else if (_fc < 0.0f || _fc > 1.0f) {
        printf("error: fir_kaiser_window(), cutoff frequency (%12.4e) out of range [0.0,1.0]\n", _fc);
        exit(0);
    } else if (_n == 0) {
        printf("error: fir_kaiser_window(), filter length must be greater than zero\n");
        exit(0);
    }

    // chooise kaiser beta parameter (approximate)
    float beta = kaiser_beta_slsl(_slsl);

    float t, h1, h2; 
    unsigned int i;
    for (i=0; i<_n; i++) {
        t = (float)i - (float)(_n-1)/2 + _mu;
     
        // sinc prototype
        h1 = sincf(_fc*t);

        // kaiser window
        h2 = kaiser(i,_n,beta,_mu);

        //printf("t = %f, h1 = %f, h2 = %f\n", t, h1, h2);

        // composite
        _h[i] = h1*h2;
    }   
}


// Design FIR doppler filter
//  _n      : filter length
//  _fd     : normalized doppler frequency (0 < _fd < 0.5)
//  _K      : Rice fading factor (K >= 0)
//  _theta  : LoS component angle of arrival
//  _h      : output coefficient buffer
void fir_design_doppler(unsigned int _n,
                        float _fd,
                        float _K,
                        float _theta,
                        float *_h)
{
    float t, J, r, w;
    float beta = 4; // kaiser window parameter
    unsigned int i;
    for (i=0; i<_n; i++) {
        // time sample
        t = (float)i - (float)(_n-1)/2;

        // Bessel
        J = 1.5*besselj_0(fabsf(2*M_PI*_fd*t));

        // Rice-K component
        r = 1.5*_K/(_K+1)*cosf(2*M_PI*_fd*t*cosf(_theta));

        // Window
        w = kaiser(i, _n, beta, 0);

        // composite
        _h[i] = (J+r)*w;

        //printf("t=%f, J=%f, r=%f, w=%f\n", t, J, r, w);
    }
}

// Design optimum FIR root-nyquist filter
//  _n      : filter length
//  _k      : samples/symbol
//  _beta   : excess bandwidth factor
void fir_design_optim_root_nyquist(unsigned int _n,
                                   unsigned int _k,
                                   float _slsl,
                                   float *_h)
{
    // validate inputs:
    //    _k >= 2
    //    _slsl < 0

    // begin with prototype
    //float fc = 1/((float)_k);
    //fir_design_windowed_sinc(_n, fc, _slsl, _h);

    // begin optimization:
}

// filter analysis


// liquid_filter_autocorr()
//
// Compute auto-correlation of filter at a specific lag.
//
//  _h      :   filter coefficients [size: _h_len]
//  _h_len  :   filter length
//  _lag    :   auto-correlation lag (samples)
float liquid_filter_autocorr(float * _h,
                             unsigned int _h_len,
                             int _lag)
{
    // auto-correlation is even symmetric
    _lag = abs(_lag);

    // lag outside of filter length is zero
    if (_lag >= _h_len) return 0.0f;

    // compute auto-correlation
    float rxx=0.0f; // initialize auto-correlation to zero
    unsigned int i;
    for (i=_lag; i<_h_len; i++)
        rxx += _h[i] * _h[i-_lag];

    return rxx;
}

// liquid_filter_isi()
//
// Compute inter-symbol interference (ISI)--both MSE and
// maximum--for the filter _h.
//
//  _h      :   filter coefficients [size: 2*_k*_m+1]
//  _k      :   filter over-sampling rate (samples/symbol)
//  _m      :   filter delay (symbols)
//  _mse    :   output mean-squared ISI
//  _max    :   maximum ISI
void liquid_filter_isi(float * _h,
                       unsigned int _k,
                       unsigned int _m,
                       float * _mse,
                       float * _max)
{
    unsigned int h_len = 2*_k*_m+1;

    // compute zero-lag auto-correlation
    float rxx0 = liquid_filter_autocorr(_h,h_len,0);
    //printf("rxx0 = %12.8f\n", rxx0);
    //exit(1);

    unsigned int i;
    float isi_mse = 0.0f;
    float isi_max = 0.0f;
    float e;
    for (i=1; i<=2*_m; i++) {
        e = liquid_filter_autocorr(_h,h_len,i*_k) / rxx0;
        e = fabsf(e);

        isi_mse += e*e;
        
        if (i==1 || e > isi_max)
            isi_max = e;
    }

    *_mse = isi_mse / (float)(2*_m);
    *_max = isi_max;
}



