/*
 *  Copyright (c) by Ramu Ramanathan and Allin Cottrell
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* statistical tables for gretl */

#ifdef OS_WIN32
# include "../winconfig.h"
#else
# include "../config.h"
#endif

#include "libgretl.h"

typedef struct {
    int n;
    double dval[10];
} dw_t;

typedef struct {
    int df;
    double crit[5];
} dfstat_t;

#define NDW 38
#define NTSTAT 101
#define NCHI 100

dw_t dw_vals[NDW] = {
    {15,{1.08,1.36,.95,1.54,.82,1.75,.69,1.97,.56,2.21}},
    {16,{1.10,1.37,.98,1.54,.86,1.73,.74,1.93,.62,2.15}},
    {17,{1.13,1.38,1.02,1.54,.90,1.71,.78,1.90,.67,2.10}},
    {18,{1.16,1.39,1.05,1.53,.93,1.69,.82,1.87,.71,2.06}},
    {19,{1.18,1.40,1.08,1.53,.97,1.68,.86,1.85,.75,2.02}},
    {20,{1.20,1.41,1.10,1.54,1.00,1.68,.90,1.83,.79,1.99}},
    {21,{1.22,1.42,1.13,1.54,1.03,1.67,.93,1.81,.83,1.96}},
    {22,{1.24,1.43,1.15,1.54,1.05,1.66,.96,1.80,.86,1.94}},
    {23,{1.26,1.44,1.17,1.54,1.08,1.66,.99,1.79,.90,1.92}},
    {24,{1.27,1.45,1.19,1.55,1.10,1.66,1.01,1.78,.93,1.90}},
    {25,{1.29,1.45,1.21,1.55,1.12,1.66,1.04,1.77,.95,1.89}},
    {26,{1.30,1.46,1.22,1.55,1.14,1.65,1.06,1.76,.98,1.88}},
    {27,{1.32,1.47,1.24,1.56,1.16,1.65,1.08,1.76,1.01,1.86}},
    {28,{1.33,1.48,1.26,1.56,1.18,1.65,1.10,1.75,1.03,1.85}},
    {29,{1.34,1.48,1.27,1.56,1.20,1.65,1.12,1.74,1.05,1.84}},
    {30,{1.35,1.49,1.28,1.57,1.21,1.65,1.14,1.74,1.07,1.83}},
    {31,{1.36,1.50,1.30,1.57,1.23,1.65,1.16,1.74,1.09,1.83}},
    {32,{1.37,1.50,1.31,1.57,1.24,1.65,1.18,1.73,1.11l,1.82}},
    {33,{1.38,1.51,1.32,1.58,1.26,1.65,1.19,1.73,1.13,1.81}},
    {34,{1.39,1.51,1.33,1.58,1.27,1.65,1.21,1.73,1.15,1.81}},
    {35,{1.40,1.52,1.34,1.53,1.28,1.65,1.22,1.73,1.16,1.80}},
    {36,{1.41,1.52,1.35,1.59,1.29,1.65,1.24,1.73,1.18,1.80}},
    {37,{1.42,1.53,1.36,1.59,1.31,1.66,1.25,1.72,1.19,1.80}},
    {38,{1.43,1.54,1.37,1.59,1.32,1.66,1.26,1.72,1.21,1.79}},
    {39,{1.43,1.54,1.38,1.60,1.33,1.66,1.27,1.72,1.22,1.79}},
    {40,{1.44,1.54,1.39,1.60,1.34,1.66,1.29,1.72,1.23,1.79}},
    {45,{1.48,1.57,1.43,1.62,1.38,1.67,1.34,1.72,1.29,1.78}},
    {50,{1.50,1.59,1.46,1.63,1.42,1.67,1.38,1.72,1.34,1.77}},
    {55,{1.53,1.60,1.49,1.64,1.45,1.68,1.41,1.72,1.38,1.77}},
    {60,{1.55,1.62,1.51,1.65,1.48,1.69,1.44,1.73,1.41,1.77}},
    {65,{1.57,1.63,1.54,1.66,1.50,1.70,1.47,1.73,1.44,1.77}},
    {70,{1.58,1.64,1.55,1.67,1.52,1.70,1.49,1.74,1.46,1.77}},
    {75,{1.60,1.65,1.57,1.68,1.54,1.71,1.51,1.74,1.49,1.77}},
    {80,{1.61,1.66,1.59,1.69,1.56,1.72,1.53,1.74,1.51,1.77}},
    {85,{1.62,1.67,1.60,1.70,1.57,1.72,1.55,1.75,1.52,1.77}},
    {90,{1.63,1.68,1.61,1.70,1.59,1.73,1.57,1.75,1.54,1.78}},
    {95,{1.64,1.69,1.62,1.71,1.60,1.73,1.58,1.75,1.56,1.78}},
    {100,{1.65,1.69,1.63,1.72,1.61,1.74,1.59,1.76,1.57,1.78}}};

/* source for t-dist table:
   http://www.itl.nist.gov/div898/handbook/eda/section3/eda3672.htm
*/
                          /* 0.10  0.05  0.025  0.01  0.001 */
dfstat_t t_vals[NTSTAT] = {{1,{3.078,6.314,12.706,31.821,318.313}},
			  {2,{1.886,2.920,4.303,6.965,22.327}},
			  {3,{1.638,2.353,3.182,4.541,10.215}},
			  {4,{1.533,2.132,2.776,3.747,7.173}},
			  {5,{1.476,2.015,2.571,3.365,5.893}},
			  {6,{1.440,1.943,2.447,3.143,5.208}},
			  {7,{1.415,1.895,2.365,2.998,4.782}},
			  {8,{1.397,1.860,2.306,2.896,4.499}},
			  {9,{1.383,1.833,2.262,2.821,4.296}},
			  {10,{1.372,1.812,2.228,2.764,4.143}},
			  {11,{1.363,1.796,2.201,2.718,4.024}},
			  {12,{1.356,1.782,2.179,2.681,3.929}},
			  {13,{1.350,1.771,2.160,2.650,3.852}},
			  {14,{1.345,1.761,2.145,2.624,3.787}},
			  {15,{1.341,1.753,2.131,2.602,3.733}},
			  {16,{1.337,1.746,2.120,2.583,3.686}},
			  {17,{1.333,1.740,2.110,2.567,3.646}},
			  {18,{1.330,1.734,2.101,2.552,3.610}},
			  {19,{1.328,1.729,2.093,2.539,3.579}},
			  {20,{1.325,1.725,2.086,2.528,3.552}},
			  {21,{1.323,1.721,2.080,2.518,3.527}},
			  {22,{1.321,1.717,2.074,2.508,3.505}},
			  {23,{1.319,1.714,2.069,2.500,3.485}},
			  {24,{1.318,1.711,2.064,2.492,3.467}},
			  {25,{1.316,1.708,2.060,2.485,3.450}},
			  {26,{1.315,1.706,2.056,2.479,3.435}},
			  {27,{1.314,1.703,2.052,2.473,3.421}},
			  {28,{1.313,1.701,2.048,2.467,3.408}},
			  {29,{1.311,1.699,2.045,2.462,3.396}},
			  {30,{1.310,1.697,2.042,2.457,3.385}},
			  {31,{1.309,1.696,2.040,2.453,3.375}},
			  {32,{1.309,1.694,2.037,2.449,3.365}},
			  {33,{1.308,1.692,2.035,2.445,3.356}},
			  {34,{1.307,1.691,2.032,2.441,3.348}},
			  {35,{1.306,1.690,2.030,2.438,3.340}},
			  {36,{1.306,1.688,2.028,2.434,3.333}},
			  {37,{1.305,1.687,2.026,2.431,3.326}},
			  {38,{1.304,1.686,2.024,2.429,3.319}},
			  {39,{1.304,1.685,2.023,2.426,3.313}},
			  {40,{1.303,1.684,2.021,2.423,3.307}},
			  {41,{1.303,1.683,2.020,2.421,3.301}},
			  {42,{1.302,1.682,2.018,2.418,3.296}},
			  {43,{1.302,1.681,2.017,2.416,3.291}},
			  {44,{1.301,1.680,2.015,2.414,3.286}},
			  {45,{1.301,1.679,2.014,2.412,3.281}},
			  {46,{1.300,1.679,2.013,2.410,3.277}},
			  {47,{1.300,1.678,2.012,2.408,3.273}},
			  {48,{1.299,1.677,2.011,2.407,3.269}},
			  {49,{1.299,1.677,2.010,2.405,3.265}},
			  {50,{1.299,1.676,2.009,2.403,3.261}},
			  {51,{1.298,1.675,2.008,2.402,3.258}},
			  {52,{1.298,1.675,2.007,2.400,3.255}},
			  {53,{1.298,1.674,2.006,2.399,3.251}},
			  {54,{1.297,1.674,2.005,2.397,3.248}},
			  {55,{1.297,1.673,2.004,2.396,3.245}},
			  {56,{1.297,1.673,2.003,2.395,3.242}},
			  {57,{1.297,1.672,2.002,2.394,3.239}},
			  {58,{1.296,1.672,2.002,2.392,3.237}},
			  {59,{1.296,1.671,2.001,2.391,3.234}},
			  {60,{1.296,1.671,2.000,2.390,3.232}},
			  {61,{1.296,1.670,2.000,2.389,3.229}},
			  {62,{1.295,1.670,1.999,2.388,3.227}},
			  {63,{1.295,1.669,1.998,2.387,3.225}},
			  {64,{1.295,1.669,1.998,2.386,3.223}},
			  {65,{1.295,1.669,1.997,2.385,3.220}},
			  {66,{1.295,1.668,1.997,2.384,3.218}},
			  {67,{1.294,1.668,1.996,2.383,3.216}},
			  {68,{1.294,1.668,1.995,2.382,3.214}},
			  {69,{1.294,1.667,1.995,2.382,3.213}},
			  {70,{1.294,1.667,1.994,2.381,3.211}},
			  {71,{1.294,1.667,1.994,2.380,3.209}},
			  {72,{1.293,1.666,1.993,2.379,3.207}},
			  {73,{1.293,1.666,1.993,2.379,3.206}},
			  {74,{1.293,1.666,1.993,2.378,3.204}},
			  {75,{1.293,1.665,1.992,2.377,3.202}},
			  {76,{1.293,1.665,1.992,2.376,3.201}},
			  {77,{1.293,1.665,1.991,2.376,3.199}},
			  {78,{1.292,1.665,1.991,2.375,3.198}},
			  {79,{1.292,1.664,1.990,2.374,3.197}},
			  {80,{1.292,1.664,1.990,2.374,3.195}},
			  {81,{1.292,1.664,1.990,2.373,3.194}},
			  {82,{1.292,1.664,1.989,2.373,3.193}},
			  {83,{1.292,1.663,1.989,2.372,3.191}},
			  {84,{1.292,1.663,1.989,2.372,3.190}},
			  {85,{1.292,1.663,1.988,2.371,3.189}},
			  {86,{1.291,1.663,1.988,2.370,3.188}},
			  {87,{1.291,1.663,1.988,2.370,3.187}},
			  {88,{1.291,1.662,1.987,2.369,3.185}},
			  {89,{1.291,1.662,1.987,2.369,3.184}},
			  {90,{1.291,1.662,1.987,2.368,3.183}},
			  {91,{1.291,1.662,1.986,2.368,3.182}},
			  {92,{1.291,1.662,1.986,2.368,3.181}},
			  {93,{1.291,1.661,1.986,2.367,3.180}},
			  {94,{1.291,1.661,1.986,2.367,3.179}},
			  {95,{1.291,1.661,1.985,2.366,3.178}},
			  {96,{1.290,1.661,1.985,2.366,3.177}},
			  {97,{1.290,1.661,1.985,2.365,3.176}},
			  {98,{1.290,1.661,1.984,2.365,3.175}},
			  {99,{1.290,1.660,1.984,2.365,3.175}},
			  {100,{1.290,1.660,1.984,2.364,3.174}},
			  {999,{1.282,1.645,1.960,2.326,3.090}}};

/* source for chi-square table:
   http://www.itl.nist.gov/div898/handbook/eda/section3/eda3674.htm
*/
                          /* 0.10  0.05  0.025  0.01  0.001 */
dfstat_t chi_vals[NCHI] = {{1,{2.706,3.841,5.024,6.635,10.828}},
			   {2,{4.605,5.991,7.378,9.210,13.816}},
			   {3,{6.251,7.815,9.348,11.345,16.266}},
			   {4,{7.779,9.488,11.143,13.277,18.467}},
			   {5,{9.236,11.070,12.833,15.086,20.515}},
			   {6,{10.645,12.592,14.449,16.812,22.458}},
			   {7,{12.017,14.067,16.013,18.475,24.322}},
			   {8,{13.362,15.507,17.535,20.090,26.125}},
			   {9,{14.684,16.919,19.023,21.666,27.877}},
			   {10,{15.987,18.307,20.483,23.209,29.588}},
			   {11,{17.275,19.675,21.920,24.725,31.264}},
			   {12,{18.549,21.026,23.337,26.217,32.910}},
			   {13,{19.812,22.362,24.736,27.688,34.528}},
			   {14,{21.064,23.685,26.119,29.141,36.123}},
			   {15,{22.307,24.996,27.488,30.578,37.697}},
			   {16,{23.542,26.296,28.845,32.000,39.252}},
			   {17,{24.769,27.587,30.191,33.409,40.790}},
			   {18,{25.989,28.869,31.526,34.805,42.312}},
			   {19,{27.204,30.144,32.852,36.191,43.820}},
			   {20,{28.412,31.410,34.170,37.566,45.315}},
			   {21,{29.615,32.671,35.479,38.932,46.797}},
			   {22,{30.813,33.924,36.781,40.289,48.268}},
			   {23,{32.007,35.172,38.076,41.638,49.728}},
			   {24,{33.196,36.415,39.364,42.980,51.179}},
			   {25,{34.382,37.652,40.646,44.314,52.620}},
			   {26,{35.563,38.885,41.923,45.642,54.052}},
			   {27,{36.741,40.113,43.195,46.963,55.476}},
			   {28,{37.916,41.337,44.461,48.278,56.892}},
			   {29,{39.087,42.557,45.722,49.588,58.301}},
			   {30,{40.256,43.773,46.979,50.892,59.703}},
			   {31,{41.422,44.985,48.232,52.191,61.098}},
			   {32,{42.585,46.194,49.480,53.486,62.487}},
			   {33,{43.745,47.400,50.725,54.776,63.870}},
			   {34,{44.903,48.602,51.966,56.061,65.247}},
			   {35,{46.059,49.802,53.203,57.342,66.619}},
			   {36,{47.212,50.998,54.437,58.619,67.985}},
			   {37,{48.363,52.192,55.668,59.893,69.347}},
			   {38,{49.513,53.384,56.896,61.162,70.703}},
			   {39,{50.660,54.572,58.120,62.428,72.055}},
			   {40,{51.805,55.758,59.342,63.691,73.402}},
			   {41,{52.949,56.942,60.561,64.950,74.745}},
			   {42,{54.090,58.124,61.777,66.206,76.084}},
			   {43,{55.230,59.304,62.990,67.459,77.419}},
			   {44,{56.369,60.481,64.201,68.710,78.750}},
			   {45,{57.505,61.656,65.410,69.957,80.077}},
			   {46,{58.641,62.830,66.617,71.201,81.400}},
			   {47,{59.774,64.001,67.821,72.443,82.720}},
			   {48,{60.907,65.171,69.023,73.683,84.037}},
			   {49,{62.038,66.339,70.222,74.919,85.351}},
			   {50,{63.167,67.505,71.420,76.154,86.661}},
			   {51,{64.295,68.669,72.616,77.386,87.968}},
			   {52,{65.422,69.832,73.810,78.616,89.272}},
			   {53,{66.548,70.993,75.002,79.843,90.573}},
			   {54,{67.673,72.153,76.192,81.069,91.872}},
			   {55,{68.796,73.311,77.380,82.292,93.168}},
			   {56,{69.919,74.468,78.567,83.513,94.461}},
			   {57,{71.040,75.624,79.752,84.733,95.751}},
			   {58,{72.160,76.778,80.936,85.950,97.039}},
			   {59,{73.279,77.931,82.117,87.166,98.324}},
			   {60,{74.397,79.082,83.298,88.379,99.607}},
			   {61,{75.514,80.232,84.476,89.591,100.888}},
			   {62,{76.630,81.381,85.654,90.802,102.166}},
			   {63,{77.745,82.529,86.830,92.010,103.442}},
			   {64,{78.860,83.675,88.004,93.217,104.716}},
			   {65,{79.973,84.821,89.177,94.422,105.988}},
			   {66,{81.085,85.965,90.349,95.626,107.258}},
			   {67,{82.197,87.108,91.519,96.828,108.526}},
			   {68,{83.308,88.250,92.689,98.028,109.791}},
			   {69,{84.418,89.391,93.856,99.228,111.055}},
			   {70,{85.527,90.531,95.023,100.425,112.317}},
			   {71,{86.635,91.670,96.189,101.621,113.577}},
			   {72,{87.743,92.808,97.353,102.816,114.835}},
			   {73,{88.850,93.945,98.516,104.010,116.092}},
			   {74,{89.956,95.081,99.678,105.202,117.346}},
			   {75,{91.061,96.217,100.839,106.393,118.599}},
			   {76,{92.166,97.351,101.999,107.583,119.850}},
			   {77,{93.270,98.484,103.158,108.771,121.100}},
			   {78,{94.374,99.617,104.316,109.958,122.348}},
			   {79,{95.476,100.749,105.473,111.144,123.594}},
			   {80,{96.578,101.879,106.629,112.329,124.839}},
			   {81,{97.680,103.010,107.783,113.512,126.083}},
			   {82,{98.780,104.139,108.937,114.695,127.324}},
			   {83,{99.880,105.267,110.090,115.876,128.565}},
			   {84,{100.980,106.395,111.242,117.057,129.804}},
			   {85,{102.079,107.522,112.393,118.236,131.041}},
			   {86,{103.177,108.648,113.544,119.414,132.277}},
			   {87,{104.275,109.773,114.693,120.591,133.512}},
			   {88,{105.372,110.898,115.841,121.767,134.746}},
			   {89,{106.469,112.022,116.989,122.942,135.978}},
			   {90,{107.565,113.145,118.136,124.116,137.208}},
			   {91,{108.661,114.268,119.282,125.289,138.438}},
			   {92,{109.756,115.390,120.427,126.462,139.666}},
			   {93,{110.850,116.511,121.571,127.633,140.893}},
			   {94,{111.944,117.632,122.715,128.803,142.119}},
			   {95,{113.038,118.752,123.858,129.973,143.344}},
			   {96,{114.131,119.871,125.000,131.141,144.567}},
			   {97,{115.223,120.990,126.141,132.309,145.789}},
			   {98,{116.315,122.108,127.282,133.476,147.010}},
			   {99,{117.407,123.225,128.422,134.642,148.230}},
			   {100,{118.498,124.342,129.561,135.807,149.449}}};

			  

/* .................................................................. */

static void other_tables (PRN *prn)
{
    pprintf(prn, _("\nFor more comprehensive statistical tables, please consult "
	    "a statistics or\neconometrics text, e.g. Ramanathan's "
	    "Introductory Econometrics.\n"));
}

/* .................................................................. */

void dw_lookup (int n, PRN *prn)
{
    int i, j, nlo, nhi;

    nlo = 15;
    nhi = 100;
    if (n < 15) n = 15;
    if (n > 100) n = 100;

    for (i=0; i<NDW; i++) {
	if (dw_vals[i].n <= n) nlo = dw_vals[i].n;
	if (dw_vals[i].n >= n) {
	    nhi = dw_vals[i].n;
	    break;
	}
    }

    pprintf(prn, _("5%% critical values for Durbin-Watson statistic\n\n"));
    pprintf(prn, _("              Number of explanatory variables (excluding the "
	    "constant):\n\n"));
    pprintf(prn, "               1             2             3             4"
	    "             5\n");
    pprintf(prn, "           dL     dU     dL     dU     dL     dU     dL     dU"
	    "     dL     dU\n\n");
    for (i=0; i<NDW; i++) {
	if (dw_vals[i].n >= nlo && dw_vals[i].n <= nhi) {
	    pprintf(prn, "n = %3d ", dw_vals[i].n);
	    for (j=0; j<10; j++)
		pprintf(prn, "%6.2f ", dw_vals[i].dval[j]);
	    pprintf(prn, "\n");
	}
    }
    other_tables(prn);
}

/* .................................................................. */

void norm_lookup (PRN *prn, int gui)
{
    pprintf(prn, _("Critical values for standard normal distribution\n\n"));
    pprintf(prn, _("Column headings show alpha (significance level) for "
	    "a one-tailed test.\n"));
    pprintf(prn, _("For a two-tailed test, select the column heading "
	    "showing half the desired\nalpha level.  "));
    pprintf(prn, _("(For example, for a two-tailed test using the 10%% "
	    "significance\nlevel, use the 0.05 column.)\n\n"));
    pprintf(prn, "      0.10     0.05    0.025     0.01    0.001\n\n"); 
    pprintf(prn, "  %8.3f %8.3f %8.3f %8.3f %8.3f",
	    1.282, 1.645, 1.960, 2.326, 3.090);
    if (gui) other_tables(prn);
}

/* .................................................................. */

void t_lookup (int df, PRN *prn, int gui)
{
    int i, j, dflo, dfhi;

    dflo = dfhi = 999;

    for (i=0; i<NTSTAT; i++) {
	if (t_vals[i].df <= df) dflo = t_vals[i].df;
	if (t_vals[i].df >= df) {
	    dfhi = t_vals[i].df;
	    break;
	}
    }

    pprintf(prn, _("Critical values for Student's t distribution\n\n"));
    pprintf(prn, _("Column headings show alpha (significance level) for "
	    "a one-tailed test.\n"));
    pprintf(prn, _("For a two-tailed test, select the column heading "
	    "showing half the desired\nalpha level.  "));
    pprintf(prn, _("(For example, for a two-tailed test using the 10%% "
	    "significance\nlevel, use the 0.05 column.)\n\n"));
    pprintf(prn, "             0.10     0.05    0.025     0.01    0.001\n\n"); 
    for (i=0; i<NTSTAT; i++) {
	if (t_vals[i].df >= dflo && t_vals[i].df <= dfhi) {
	    pprintf(prn, "df = ");
	    if (t_vals[i].df == 999)
		pprintf(prn, _("inf."));
	    else
		pprintf(prn, "%3d ", t_vals[i].df);
	    for (j=0; j<5; j++)
		pprintf(prn, "%8.3f ", t_vals[i].crit[j]);
	    pprintf(prn, "\n");
	}
    }
    if (gui) other_tables(prn);
}

/* .................................................................. */

void chisq_lookup (int df, PRN *prn, int gui)
{
    int i, j;

    if (df > 100) df = 100;

    pprintf(prn, _("Critical values for Chi-square distribution\n\n"));
    pprintf(prn, _("Column headings show alpha (significance level) for "
	    "a one-tailed test.\n\n"));
    pprintf(prn, "             0.10     0.05    0.025     0.01    0.001\n\n"); 
    for (i=0; i<NCHI; i++) {
	if (chi_vals[i].df == df) {
	    pprintf(prn, "df = %3d ", df);
	    for (j=0; j<5; j++)
		pprintf(prn, "%8.3f ", chi_vals[i].crit[j]);
	    pprintf(prn, "\n");
	}
    }
    if (gui) other_tables(prn);
}



