/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef PCC_CONFIG_H
#define PCC_CONFIG_H

#include <stdint.h>

/*
struct pcc_coeff -- structure for providing the PCC (Polynomial Color Correction)
coefficients

		Rout = Rc + RrRin + RgGin + RbBin + RrrRinRin + RggGinGin + RbbBinBin +
					 RrgRinGin + RgbGinBin + RrbRinBin + RrgbRinGinBin

		Gout = Gc + GrRin + GgGin + GbBin + GrrRinRin + GggGinGin + GbbBinBin +
					 GrgRinGin + GgbGinBin + GrbRinBin + GrgbRinGinBin

		Bout = Bc + BrRin + BgGin + BbBin + BrrRinRin + BggGinGin + BbbBinBin +
					 BrgRinGin + BgbGinBin + BrbRinBin + BrgbRinGinBin

	X = R, G or B
	c   -- Xc coefficent
	r   -- Xr coefficent
	g   -- Xg coefficent
	b   -- Xb coefficent
	rr  -- Xrr coefficent
	gg  -- Xgg coefficent
	bb  -- Xbb coefficent
	rg  -- Xrg coefficent
	gb  -- Xgb coefficent
	rb  -- Xrb coefficent
	rgb -- Xrgb coefficent
*/
struct pcc_coeff {
	double c;
	double r;
	double g;
	double b;
	double rr;
	double gg;
	double bb;
	double rg;
	double gb;
	double rb;
	double rgb;
};

/*
struct pcc_coeff_data -- structure for providing per color component coefficients
flags -- Reserved
r     -- Red component coefficents
g     -- Green component coefficents
b     -- Blue component coefficents
*/
struct pcc_coeff_data {
	uint32_t flags;
	struct pcc_coeff r;
	struct pcc_coeff g;
	struct pcc_coeff b;
};

#endif /* PCC_CONFIG_H */
