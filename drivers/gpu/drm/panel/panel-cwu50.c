// SPDX-License-Identifier: GPL-2.0+

#include <drm/drm_modes.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>

struct cwu50_desc {
	bool detect_panel_type;
};

struct cwu50 {
	struct device *dev;
	struct drm_panel panel;
	struct regulator *vci;
	struct regulator *iovcc;
	struct gpio_desc *reset_gpio;
	const struct cwu50_desc *desc;
	bool prepared;
	bool reset_controllable;
	bool is_new_panel;
	bool vci_enabled;
	bool iovcc_enabled;
	enum drm_panel_orientation orientation;
};

#define CWU50_VCC_TO_LP11_MS		5
#define CWU50_LP11_TO_RESET_MS		10
#define CWU50_RESET_ASSERT_MS		10
#define CWU50_RESET_DEASSERT_MS		5
#define CWU50_RESET_TO_SLEEP_OUT_MS	120
#define CWU50_SLEEP_IN_MS		120
#define CWU50_SLEEP_OUT_MS		120
#define CWU50_DISPLAY_ON_MS		20

static const struct drm_display_mode default_mode = {
	.clock = 61020,
	.hdisplay = 720,
	.hsync_start = 720 + 30,
	.hsync_end = 720+ 30 + 15,
	.htotal = 720 + 30 + 15 + 15,
	.vdisplay = 1280,
	.vsync_start = 1280 + 8,
	.vsync_end = 1280 + 8+ 2,
	.vtotal = 1280 + 8 + 2 + 16,
	.width_mm = 63,
	.height_mm = 110,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static inline struct cwu50 *panel_to_cwu50(struct drm_panel *panel)
{
	return container_of(panel, struct cwu50, panel);
}

#define dcs_write_seq(seq...) mipi_dsi_dcs_write_seq_multi(&dsi_ctx, seq)

static int cwu50_init_sequence(struct cwu50 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = {
		.dsi = to_mipi_dsi_device(ctx->dev),
	};

	dcs_write_seq(0xE1,0x93);
	dcs_write_seq(0xE2,0x65);
	dcs_write_seq(0xE3,0xF8);
	dcs_write_seq(0x70,0x20);
	dcs_write_seq(0x71,0x13);
	dcs_write_seq(0x72,0x06);
	dcs_write_seq(0x75,0x03);
	dcs_write_seq(0xE0,0x01);
	dcs_write_seq(0x00,0x00);
	dcs_write_seq(0x01,0x47);//VCOM0x47
	dcs_write_seq(0x03,0x00);
	dcs_write_seq(0x04,0x4D);
	dcs_write_seq(0x0C,0x64);
	dcs_write_seq(0x17,0x00);
	dcs_write_seq(0x18,0xBF);
	dcs_write_seq(0x19,0x00);
	dcs_write_seq(0x1A,0x00);
	dcs_write_seq(0x1B,0xBF);
	dcs_write_seq(0x1C,0x00);
	dcs_write_seq(0x1F,0x7E);
	dcs_write_seq(0x20,0x24);
	dcs_write_seq(0x21,0x24);
	dcs_write_seq(0x22,0x4E);
	dcs_write_seq(0x24,0xFE);
	dcs_write_seq(0x37,0x09);
	dcs_write_seq(0x38,0x04);
	dcs_write_seq(0x3C,0x76);
	dcs_write_seq(0x3D,0xFF);
	dcs_write_seq(0x3E,0xFF);
	dcs_write_seq(0x3F,0x7F);
	dcs_write_seq(0x40,0x04);//Dot inversion type
	dcs_write_seq(0x41,0xA0);
	dcs_write_seq(0x44,0x11);
	dcs_write_seq(0x55,0x02);
	dcs_write_seq(0x56,0x01);
	dcs_write_seq(0x57,0x49);
	dcs_write_seq(0x58,0x09);
	dcs_write_seq(0x59,0x2A);
	dcs_write_seq(0x5A,0x1A);
	dcs_write_seq(0x5B,0x1A);
	dcs_write_seq(0x5D,0x78);
	dcs_write_seq(0x5E,0x6E);
	dcs_write_seq(0x5F,0x66);
	dcs_write_seq(0x60,0x5E);
	dcs_write_seq(0x61,0x60);
	dcs_write_seq(0x62,0x54);
	dcs_write_seq(0x63,0x5C);
	dcs_write_seq(0x64,0x47);
	dcs_write_seq(0x65,0x5F);
	dcs_write_seq(0x66,0x5D);
	dcs_write_seq(0x67,0x5B);
	dcs_write_seq(0x68,0x76);
	dcs_write_seq(0x69,0x61);
	dcs_write_seq(0x6A,0x63);
	dcs_write_seq(0x6B,0x50);
	dcs_write_seq(0x6C,0x45);
	dcs_write_seq(0x6D,0x34);
	dcs_write_seq(0x6E,0x1C);
	dcs_write_seq(0x6F,0x07);
	dcs_write_seq(0x70,0x78);
	dcs_write_seq(0x71,0x6E);
	dcs_write_seq(0x72,0x66);
	dcs_write_seq(0x73,0x5E);
	dcs_write_seq(0x74,0x60);
	dcs_write_seq(0x75,0x54);
	dcs_write_seq(0x76,0x5C);
	dcs_write_seq(0x77,0x47);
	dcs_write_seq(0x78,0x5F);
	dcs_write_seq(0x79,0x5D);
	dcs_write_seq(0x7A,0x5B);
	dcs_write_seq(0x7B,0x76);
	dcs_write_seq(0x7C,0x61);
	dcs_write_seq(0x7D,0x63);
	dcs_write_seq(0x7E,0x50);
	dcs_write_seq(0x7F,0x45);
	dcs_write_seq(0x80,0x34);
	dcs_write_seq(0x81,0x1C);
	dcs_write_seq(0x82,0x07);
	dcs_write_seq(0xE0,0x02);
	dcs_write_seq(0x00,0x44);
	dcs_write_seq(0x01,0x46);
	dcs_write_seq(0x02,0x48);
	dcs_write_seq(0x03,0x4A);
	dcs_write_seq(0x04,0x40);
	dcs_write_seq(0x05,0x42);
	dcs_write_seq(0x06,0x1F);
	dcs_write_seq(0x07,0x1F);
	dcs_write_seq(0x08,0x1F);
	dcs_write_seq(0x09,0x1F);
	dcs_write_seq(0x0A,0x1F);
	dcs_write_seq(0x0B,0x1F);
	dcs_write_seq(0x0C,0x1F);
	dcs_write_seq(0x0D,0x1F);
	dcs_write_seq(0x0E,0x1F);
	dcs_write_seq(0x0F,0x1F);
	dcs_write_seq(0x10,0x1F);
	dcs_write_seq(0x11,0x1F);
	dcs_write_seq(0x12,0x1F);
	dcs_write_seq(0x13,0x1F);
	dcs_write_seq(0x14,0x1E);
	dcs_write_seq(0x15,0x1F);
	dcs_write_seq(0x16,0x45);
	dcs_write_seq(0x17,0x47);
	dcs_write_seq(0x18,0x49);
	dcs_write_seq(0x19,0x4B);
	dcs_write_seq(0x1A,0x41);
	dcs_write_seq(0x1B,0x43);
	dcs_write_seq(0x1C,0x1F);
	dcs_write_seq(0x1D,0x1F);
	dcs_write_seq(0x1E,0x1F);
	dcs_write_seq(0x1F,0x1F);
	dcs_write_seq(0x20,0x1F);
	dcs_write_seq(0x21,0x1F);
	dcs_write_seq(0x22,0x1F);
	dcs_write_seq(0x23,0x1F);
	dcs_write_seq(0x24,0x1F);
	dcs_write_seq(0x25,0x1F);
	dcs_write_seq(0x26,0x1F);
	dcs_write_seq(0x27,0x1F);
	dcs_write_seq(0x28,0x1F);
	dcs_write_seq(0x29,0x1F);
	dcs_write_seq(0x2A,0x1E);
	dcs_write_seq(0x2B,0x1F);
	dcs_write_seq(0x2C,0x0B);
	dcs_write_seq(0x2D,0x09);
	dcs_write_seq(0x2E,0x07);
	dcs_write_seq(0x2F,0x05);
	dcs_write_seq(0x30,0x03);
	dcs_write_seq(0x31,0x01);
	dcs_write_seq(0x32,0x1F);
	dcs_write_seq(0x33,0x1F);
	dcs_write_seq(0x34,0x1F);
	dcs_write_seq(0x35,0x1F);
	dcs_write_seq(0x36,0x1F);
	dcs_write_seq(0x37,0x1F);
	dcs_write_seq(0x38,0x1F);
	dcs_write_seq(0x39,0x1F);
	dcs_write_seq(0x3A,0x1F);
	dcs_write_seq(0x3B,0x1F);
	dcs_write_seq(0x3C,0x1F);
	dcs_write_seq(0x3D,0x1F);
	dcs_write_seq(0x3E,0x1F);
	dcs_write_seq(0x3F,0x1F);
	dcs_write_seq(0x40,0x1F);
	dcs_write_seq(0x41,0x1E);
	dcs_write_seq(0x42,0x0A);
	dcs_write_seq(0x43,0x08);
	dcs_write_seq(0x44,0x06);
	dcs_write_seq(0x45,0x04);
	dcs_write_seq(0x46,0x02);
	dcs_write_seq(0x47,0x00);
	dcs_write_seq(0x48,0x1F);
	dcs_write_seq(0x49,0x1F);
	dcs_write_seq(0x4A,0x1F);
	dcs_write_seq(0x4B,0x1F);
	dcs_write_seq(0x4C,0x1F);
	dcs_write_seq(0x4D,0x1F);
	dcs_write_seq(0x4E,0x1F);
	dcs_write_seq(0x4F,0x1F);
	dcs_write_seq(0x50,0x1F);
	dcs_write_seq(0x51,0x1F);
	dcs_write_seq(0x52,0x1F);
	dcs_write_seq(0x53,0x1F);
	dcs_write_seq(0x54,0x1F);
	dcs_write_seq(0x55,0x1F);
	dcs_write_seq(0x56,0x1F);
	dcs_write_seq(0x57,0x1E);
	dcs_write_seq(0x58,0x40);
	dcs_write_seq(0x59,0x00);
	dcs_write_seq(0x5A,0x00);
	dcs_write_seq(0x5B,0x30);
	dcs_write_seq(0x5C,0x02);
	dcs_write_seq(0x5D,0x40);
	dcs_write_seq(0x5E,0x01);
	dcs_write_seq(0x5F,0x02);
	dcs_write_seq(0x60,0x00);
	dcs_write_seq(0x61,0x01);
	dcs_write_seq(0x62,0x02);
	dcs_write_seq(0x63,0x65);
	dcs_write_seq(0x64,0x66);
	dcs_write_seq(0x65,0x00);
	dcs_write_seq(0x66,0x00);
	dcs_write_seq(0x67,0x74);
	dcs_write_seq(0x68,0x06);
	dcs_write_seq(0x69,0x65);
	dcs_write_seq(0x6A,0x66);
	dcs_write_seq(0x6B,0x10);
	dcs_write_seq(0x6C,0x00);
	dcs_write_seq(0x6D,0x04);
	dcs_write_seq(0x6E,0x04);
	dcs_write_seq(0x6F,0x88);
	dcs_write_seq(0x70,0x00);
	dcs_write_seq(0x71,0x00);
	dcs_write_seq(0x72,0x06);
	dcs_write_seq(0x73,0x7B);
	dcs_write_seq(0x74,0x00);
	dcs_write_seq(0x75,0x87);
	dcs_write_seq(0x76,0x00);
	dcs_write_seq(0x77,0x5D);
	dcs_write_seq(0x78,0x17);
	dcs_write_seq(0x79,0x1F);
	dcs_write_seq(0x7A,0x00);
	dcs_write_seq(0x7B,0x00);
	dcs_write_seq(0x7C,0x00);
	dcs_write_seq(0x7D,0x03);
	dcs_write_seq(0x7E,0x7B);
	dcs_write_seq(0xE0,0x04);
	dcs_write_seq(0x09,0x10);
	dcs_write_seq(0xE0, 0x00);
	dcs_write_seq(0xE6,0x02);
	dcs_write_seq(0xE7,0x02);

	return dsi_ctx.accum_err;
}

static int cwu50_init_sequence2(struct cwu50 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = {
		.dsi = to_mipi_dsi_device(ctx->dev),
	};

	dcs_write_seq(0xE0, 0x00);

	//--- PASSWORD	----//
	dcs_write_seq(0xE1,0x93);
	dcs_write_seq(0xE2,0x65);
	dcs_write_seq(0xE3,0xF8);
	dcs_write_seq(0x80,0x03);//03:4lane 02:3lane 01:2lane


	//--- Page1  ----//
	dcs_write_seq(0xE0,0x01);

	//Set VCOM
	dcs_write_seq(0x00,0x00);
	dcs_write_seq(0x01,0x62);
	dcs_write_seq(0x03,0x10);
	dcs_write_seq(0x04,0x6A);

	//Set Gamma Power, VGMP,VGMN,VGSP,VGSN
	dcs_write_seq(0x17,0x00);
	dcs_write_seq(0x18,0xDF); // VGMP=4.9V
	dcs_write_seq(0x19,0x01); // VGSP=0.3V
	dcs_write_seq(0x1A,0x00);
	dcs_write_seq(0x1B,0xDF); // VGMN=-4.9V
	dcs_write_seq(0x1C,0x01); // VGSN=0.3V

	//VCL
	dcs_write_seq(0x24,0xFE);

	//Set Panel
	dcs_write_seq(0x37,0x09);	//SS=1,BGR=1

	//SET RGBCYC
	dcs_write_seq(0x38,0x04);	//JDT=100 column inversion
	dcs_write_seq(0x39,0x08);	//RGB_N_EQ1
	dcs_write_seq(0x3A,0x12);	//RGB_N_EQ2
	dcs_write_seq(0x3C,0x78);	//SET EQ3 for TE_H
	dcs_write_seq(0x3D,0xFF);
	dcs_write_seq(0x3E,0xFF);
	dcs_write_seq(0x3F,0xFF);

	//Set TCON
	dcs_write_seq(0x40,0x04);	//RSO 04h=720, 05h=768, 06h=800
	dcs_write_seq(0x41,0xA0);	//LN=640->1280 line
	dcs_write_seq(0x42,0x7F);  //SLT=12.7us
	dcs_write_seq(0x43,0x10);  //VFP
	dcs_write_seq(0x44,0x17);  //VBP =24
	dcs_write_seq(0x45,0x40);

	//dcs_write_seq(0x4A,0x35);//BIST MODE 35:AUTO

	//--- power voltage  ----//
	dcs_write_seq(0x55,0x02);	//DCDCM=0011, JD5001
	//dcs_write_seq(0x56,0x01);
	dcs_write_seq(0x57,0x69);
	//dcs_write_seq(0x58,0x0A);
	dcs_write_seq(0x59,0x2A);	//VCL = -2.7V, AVEE=-5.5V
	dcs_write_seq(0x5A,0x1A);	//VGH = +12.2V
	dcs_write_seq(0x5B,0x1A);	//VGL = -12.2V

	//--- Gamma2.2	----//	//G2.2	  //G2.5
	dcs_write_seq(0x5D,0x7F);  //0x7F	//0x7F
	dcs_write_seq(0x5E,0x67);  //0x67	//0x65
	dcs_write_seq(0x5F,0x58);  //0x58	//0x55
	dcs_write_seq(0x60,0x4B);  //0x4B	//0x47
	dcs_write_seq(0x61,0x47);  //0x47	//0x42
	dcs_write_seq(0x62,0x39);  //0x39	//0x32
	dcs_write_seq(0x63,0x3D);  //0x3D	//0x36
	dcs_write_seq(0x64,0x25);  //0x25	//0x1D
	dcs_write_seq(0x65,0x3D);  //0x3D	//0x36
	dcs_write_seq(0x66,0x3C);  //0x3C	//0x34
	dcs_write_seq(0x67,0x3C);  //0x3C	//0x35
	dcs_write_seq(0x68,0x5B);  //0x5B	//0x51
	dcs_write_seq(0x69,0x4A);  //0x4A	//0x3D
	dcs_write_seq(0x6A,0x50);  //0x50	//0x40
	dcs_write_seq(0x6B,0x42);  //0x42	//0x31
	dcs_write_seq(0x6C,0x3B);  //0x3B	//0x2C
	dcs_write_seq(0x6D,0x2D);  //0x2D	//0x1F
	dcs_write_seq(0x6E,0x19);  //0x19	//0x0E
	dcs_write_seq(0x6F,0x00);  //0x00	//0x00
	dcs_write_seq(0x70,0x7F);  //0x7F	//0x7F
	dcs_write_seq(0x71,0x67);  //0x67	//0x65
	dcs_write_seq(0x72,0x58);  //0x58	//0x55
	dcs_write_seq(0x73,0x4B);  //0x4B	//0x47
	dcs_write_seq(0x74,0x47);  //0x47	//0x42
	dcs_write_seq(0x75,0x39);  //0x39	//0x32
	dcs_write_seq(0x76,0x3D);  //0x3D	//0x36
	dcs_write_seq(0x77,0x25);  //0x25	//0x1D
	dcs_write_seq(0x78,0x3D);  //0x3D	//0x36
	dcs_write_seq(0x79,0x3C);  //0x3C	//0x34
	dcs_write_seq(0x7A,0x3C);  //0x3C	//0x35
	dcs_write_seq(0x7B,0x5B);  //0x5B	//0x51
	dcs_write_seq(0x7C,0x4A);  //0x4A	//0x3D
	dcs_write_seq(0x7D,0x50);  //0x50	//0x40
	dcs_write_seq(0x7E,0x42);  //0x42	//0x31
	dcs_write_seq(0x7F,0x3B);  //0x3B	//0x2C
	dcs_write_seq(0x80,0x2D);  //0x2D	//0x1F
	dcs_write_seq(0x81,0x19);  //0x19	//0x0E
	dcs_write_seq(0x82,0x00);  //0x00	//0x00


	//Page2, for GIP
	dcs_write_seq(0xE0,0x02);

	//GIP_L Pin mapping
	dcs_write_seq(0x00,0x5F);	//GCL
	dcs_write_seq(0x01,0x5F);	//VSS->VGL
	dcs_write_seq(0x02,0x44);	//CLK1->CKV0
	dcs_write_seq(0x03,0x46);	//CKK3->CKV2
	dcs_write_seq(0x04,0x48);	//CLK5->CKV4
	dcs_write_seq(0x05,0x4A);	//CLK7->CKV6
	dcs_write_seq(0x06,0x5F);	//VGL
	dcs_write_seq(0x07,0x5F);	//VGL
	dcs_write_seq(0x08,0x5F);	//VGL
	dcs_write_seq(0x09,0x5F);	//NC
	dcs_write_seq(0x0A,0x5F);	//NC
	dcs_write_seq(0x0B,0x5F);	//NC //
	dcs_write_seq(0x0C,0x5F);	//NC
	dcs_write_seq(0x0D,0x5F);	//NC
	dcs_write_seq(0x0E,0x5F);	//NC //
	dcs_write_seq(0x0F,0x5F);	//NC
	dcs_write_seq(0x10,0x5F);	//NC
	dcs_write_seq(0x11,0x5F);	//NC //
	dcs_write_seq(0x12,0x5E);	//GCH
	dcs_write_seq(0x13,0x5E);	//VDD->VGH
	dcs_write_seq(0x14,0x40);	//STV1
	dcs_write_seq(0x15,0x42);	//STV3

	//GIP_R Pin mapping
	dcs_write_seq(0x16,0x5F);	//GCL
	dcs_write_seq(0x17,0x5F);	//VSS->VGL
	dcs_write_seq(0x18,0x45);	//CLK2->CKV1
	dcs_write_seq(0x19,0x47);	//CKK4->CKV3
	dcs_write_seq(0x1A,0x49);	//CLK6->CKV5
	dcs_write_seq(0x1B,0x4B);	//CLK8->CKV7
	dcs_write_seq(0x1C,0x5F);	//VGL
	dcs_write_seq(0x1D,0x5F);	//VGL
	dcs_write_seq(0x1E,0x5F);	//VGL
	dcs_write_seq(0x1F,0x5F);	//NC
	dcs_write_seq(0x20,0x5F);	//NC
	dcs_write_seq(0x21,0x5F);	//NC //
	dcs_write_seq(0x22,0x5F);	//NC
	dcs_write_seq(0x23,0x5F);	//NC
	dcs_write_seq(0x24,0x5F);	//NC //
	dcs_write_seq(0x25,0x5F);	//NC
	dcs_write_seq(0x26,0x5F);	//NC
	dcs_write_seq(0x27,0x5F);	//NC //
	dcs_write_seq(0x28,0x5E);	//GCH
	dcs_write_seq(0x29,0x5E);	//VDD->VGH
	dcs_write_seq(0x2A,0x41);	//STV2
	dcs_write_seq(0x2B,0x43);	//STV4

	//GIP_L_GS Pin mapping
	dcs_write_seq(0x2C,0x1F);	//GCL
	dcs_write_seq(0x2D,0x1E);	//VSS->VGH
	dcs_write_seq(0x2E,0x0B);	//CLK1->CKV7
	dcs_write_seq(0x2F,0x09);	//CKK3->CKV5
	dcs_write_seq(0x30,0x07);	//CLK5->CKV3
	dcs_write_seq(0x31,0x05);	//CLK7->CKV1
	dcs_write_seq(0x32,0x1F);	//VGL
	dcs_write_seq(0x33,0x1F);	//VGL
	dcs_write_seq(0x34,0x1F);	//VGL
	dcs_write_seq(0x35,0x1F);	//NC
	dcs_write_seq(0x36,0x1F);	//NC
	dcs_write_seq(0x37,0x1F);	//NC //
	dcs_write_seq(0x38,0x1F);	//NC
	dcs_write_seq(0x39,0x1F);	//NC
	dcs_write_seq(0x3A,0x1F);	//NC //
	dcs_write_seq(0x3B,0x1F);	//NC
	dcs_write_seq(0x3C,0x1F);	//NC
	dcs_write_seq(0x3D,0x1F);	//NC //
	dcs_write_seq(0x3E,0x1E);	//GCH
	dcs_write_seq(0x3F,0x1F);	//VDD->VGL
	dcs_write_seq(0x40,0x03);	//STV1
	dcs_write_seq(0x41,0x01);	//STV3

	//GIP_R_GS Pin mapping
	dcs_write_seq(0x42,0x1F);	//GCL
	dcs_write_seq(0x43,0x1E);	//VSS->VGH
	dcs_write_seq(0x44,0x0A);	//CLK2->CKV6
	dcs_write_seq(0x45,0x08);	//CKK4->CKV4
	dcs_write_seq(0x46,0x06);	//CLK6->CKV2
	dcs_write_seq(0x47,0x04);	//CLK8->CKV0
	dcs_write_seq(0x48,0x1F);	//VGL
	dcs_write_seq(0x49,0x1F);	//VGL
	dcs_write_seq(0x4A,0x1F);	//VGL
	dcs_write_seq(0x4B,0x1F);	//NC
	dcs_write_seq(0x4C,0x1F);	//NC
	dcs_write_seq(0x4D,0x1F);	//NC //
	dcs_write_seq(0x4E,0x1F);	//NC
	dcs_write_seq(0x4F,0x1F);	//NC
	dcs_write_seq(0x50,0x1F);	//NC //
	dcs_write_seq(0x51,0x1F);	//NC
	dcs_write_seq(0x52,0x1F);	//NC
	dcs_write_seq(0x53,0x1F);	//NC //
	dcs_write_seq(0x54,0x1E);	//GCH
	dcs_write_seq(0x55,0x1F);	//VDD->VGL
	dcs_write_seq(0x56,0x02);	//STV2
	dcs_write_seq(0x57,0x00);	//STV4

	//GIP Timing
	dcs_write_seq(0x58,0x40);
	dcs_write_seq(0x59,0x00);
	dcs_write_seq(0x5A,0x00);
	dcs_write_seq(0x5B,0x30);
	dcs_write_seq(0x5C,0x0B); //STV_S0
	dcs_write_seq(0x5D,0x30);
	dcs_write_seq(0x5E,0x01);
	dcs_write_seq(0x5F,0x02);
	//dcs_write_seq(0x60,0x00);
	//dcs_write_seq(0x61,0x01);
	//dcs_write_seq(0x62,0x02);
	dcs_write_seq(0x63,0x06);
	dcs_write_seq(0x64,0x6A); //SETV_OFF
	//dcs_write_seq(0x65,0x00);
	//dcs_write_seq(0x66,0x00);
	dcs_write_seq(0x67,0x73);
	dcs_write_seq(0x68,0x0D); //CKV_S0
	dcs_write_seq(0x69,0x06);
	dcs_write_seq(0x6A,0x6A); //CKV_OFF,61(GOE=2.9)
	dcs_write_seq(0x6B,0x10);
	dcs_write_seq(0x6C,0x00);
	dcs_write_seq(0x6D,0x04);
	dcs_write_seq(0x6E,0x04);
	dcs_write_seq(0x6F,0x88);
	//dcs_write_seq(0x70,0x00);
	//dcs_write_seq(0x71,0x00);
	//dcs_write_seq(0x72,0x06);
	//dcs_write_seq(0x73,0x7B);
	//dcs_write_seq(0x74,0x00);
	//dcs_write_seq(0x75,0x07);
	//dcs_write_seq(0x76,0x00);
	//dcs_write_seq(0x77,0x5D);
	//dcs_write_seq(0x78,0x17);
	//dcs_write_seq(0x79,0x1F);
	//dcs_write_seq(0x7A,0x00);
	//dcs_write_seq(0x7B,0x00);
	//dcs_write_seq(0x7C,0x00);
	//dcs_write_seq(0x7D,0x03);
	//dcs_write_seq(0x7E,0x7B);

	//Page4
	dcs_write_seq(0xE0,0x04);
	dcs_write_seq(0x00,0x0E);
	dcs_write_seq(0x02,0xB3);
	dcs_write_seq(0x09,0x60);
	dcs_write_seq(0x0E,0x48);

	//Page0
	dcs_write_seq(0xE0,0x00);

	//--- TE----//
	dcs_write_seq(0x35,0x00);

	return dsi_ctx.accum_err;
}

static void cwu50_disable_regulators(struct cwu50 *ctx)
{
	if (ctx->vci_enabled) {
		regulator_disable(ctx->vci);
		ctx->vci_enabled = false;
	}

	if (ctx->iovcc_enabled) {
		regulator_disable(ctx->iovcc);
		ctx->iovcc_enabled = false;
	}
}

static int cwu50_enable_regulators(struct cwu50 *ctx)
{
	int ret;

	if (ctx->iovcc) {
		ret = regulator_enable(ctx->iovcc);
		if (ret) {
			dev_err(ctx->dev, "failed to enable iovcc (%d)\n", ret);
			return ret;
		}
		ctx->iovcc_enabled = true;
	}

	if (ctx->vci) {
		ret = regulator_enable(ctx->vci);
		if (ret) {
			dev_err(ctx->dev, "failed to enable vci (%d)\n", ret);
			cwu50_disable_regulators(ctx);
			return ret;
		}
		ctx->vci_enabled = true;
	}

	return 0;
}

static void cwu50_set_reset(struct cwu50 *ctx, int value)
{
	if (ctx->reset_controllable)
		gpiod_set_value_cansleep(ctx->reset_gpio, value);
}

static void cwu50_hw_reset(struct cwu50 *ctx)
{
	if (!ctx->reset_controllable)
		return;

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(CWU50_RESET_DEASSERT_MS);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(CWU50_RESET_ASSERT_MS);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(CWU50_RESET_DEASSERT_MS);
}

static int cwu50_unprepare(struct drm_panel *panel)
{
	struct cwu50 *ctx = panel_to_cwu50(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		dev_err(ctx->dev, "failed to turn display off (%d)\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(ctx->dev, "failed to enter sleep mode (%d)\n", ret);
		return ret;
	}
	msleep(CWU50_SLEEP_IN_MS);

	cwu50_set_reset(ctx, 1);
	msleep(CWU50_RESET_DEASSERT_MS);
	cwu50_disable_regulators(ctx);

	ctx->prepared = false;

	return 0;
}

static int cwu50_prepare(struct drm_panel *panel)
{
	struct cwu50 *ctx = panel_to_cwu50(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;
	u8 buf[3] = {};

	if (ctx->prepared)
		return 0;

	cwu50_set_reset(ctx, 1);

	ret = cwu50_enable_regulators(ctx);
	if (ret)
		return ret;

	/*
	 * The JD9365D power-on sequence requires the DSI lanes to be in LP-11
	 * before reset is released. Sending NOP mirrors the in-tree
	 * JD9365DA-H3 driver and forces the host out of high-speed video state.
	 */
	msleep(CWU50_VCC_TO_LP11_MS);

	ret = mipi_dsi_dcs_nop(dsi);
	if (ret) {
		dev_err(ctx->dev, "failed to force LP-11 before reset (%d)\n", ret);
		goto poweroff;
	}

	msleep(CWU50_LP11_TO_RESET_MS);

	cwu50_hw_reset(ctx);

	/*
	 * JD9365D reset timing allows commands 5ms after RESX is released,
	 * but Sleep Out must not be sent until 120ms after reset. Some regular
	 * panel variants do not expose controllable RESX here, but the delay is
	 * harmless and keeps cold boot timing conservative.
	 */
	msleep(CWU50_RESET_TO_SLEEP_OUT_MS);

	if (ctx->desc->detect_panel_type) {
		ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
		if (ret) {
			dev_err(ctx->dev, "failed to enable vblank TE (%d)\n", ret);
			goto poweroff;
		}

		ret = mipi_dsi_dcs_write(dsi, 0xe0, (u8[]){ 0x00 }, 1);
		if (ret < 0) {
			dev_warn(ctx->dev, "failed to select ID command page (%d); keeping probed panel type\n",
				 ret);
		} else {
			ret = mipi_dsi_dcs_read(dsi, 0x04, buf, sizeof(buf));
			if (ret >= 1 && buf[0] == 0x39) {
				ctx->is_new_panel = true;
			} else if (ret < 0) {
				dev_warn(ctx->dev, "failed to read panel ID (%d); keeping probed panel type\n",
					 ret);
			}
		}

		if (ctx->is_new_panel)
			ret = cwu50_init_sequence2(ctx);
		else
			ret = cwu50_init_sequence(ctx);
	} else {
		ret = cwu50_init_sequence(ctx);
	}
	if (ret) {
		dev_err(ctx->dev, "failed to send initialize sequence (%d)\n", ret);
		goto poweroff;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(ctx->dev, "failed to exit sleep mode (%d)\n", ret);
		goto poweroff;
	}
	msleep(CWU50_SLEEP_OUT_MS);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(ctx->dev, "failed to turn display on (%d)\n", ret);
		goto poweroff;
	}
	msleep(CWU50_DISPLAY_ON_MS);

	ctx->prepared = true;

	return 0;

poweroff:
	cwu50_set_reset(ctx, 1);
	cwu50_disable_regulators(ctx);
	return ret;
}

static enum drm_panel_orientation cwu50_get_orientation(struct drm_panel *panel)
{
	struct cwu50 *ctx = panel_to_cwu50(panel);

	return ctx->orientation;
}

static int cwu50_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct cwu50 *ctx = panel_to_cwu50(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	/* set up connector's "panel orientation" property */
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs cwu50_drm_funcs = {
	.unprepare = cwu50_unprepare,
	.prepare = cwu50_prepare,
	.get_modes = cwu50_get_modes,
	.get_orientation = cwu50_get_orientation,
};

static int cwu50_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct cwu50 *ctx;
	int ret, err;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	if (ctx->desc->detect_panel_type) {
		ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_IN);
		if (IS_ERR(ctx->reset_gpio))
			return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
					     "failed to request reset GPIO\n");

		ctx->is_new_panel = gpiod_get_value_cansleep(ctx->reset_gpio);
		if (ctx->is_new_panel) {
			dev_info(dev, "detected new panel type\n");
		} else {
			dev_info(dev, "detected old panel type\n");
			ret = gpiod_direction_output(ctx->reset_gpio, 1);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to switch reset GPIO to output\n");
			ctx->reset_controllable = true;
		}
	} else {
		ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio))
			return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
					     "failed to request reset GPIO\n");
		ctx->reset_controllable = true;
	}

	/*
	 * Request vci and iovcc regulators when they are defined.
	 * Even though these regulators may be always-on, we still need
	 * to ensure that the panel only becomes ready _after_ them.
	 * This is achieved by bubbling up EPROBE_DEFER from them.
	 */
	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci)) {
		err = PTR_ERR(ctx->vci);
		if (err == -EPROBE_DEFER) {
			dev_info(dev, "vci regulator isn't ready, retry later\n");
			return err;
		}

		dev_err(dev, "Failed to request vci regulator: %d\n", err);
		ctx->vci = NULL;
	}

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc)) {
		err = PTR_ERR(ctx->iovcc);
		if (err == -EPROBE_DEFER) {
			dev_info(dev, "iovcc regulator isn't ready, retry later\n");
			return err;
		}

		dev_err(dev, "Failed to request iovcc regulator: %d\n", err);
		ctx->iovcc = NULL;
	}

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	ctx->panel.prepare_prev_first = true;

	drm_panel_init(&ctx->panel, dev, &cwu50_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static const struct cwu50_desc cwu50_desc = {
	.detect_panel_type = true,
};

static const struct cwu50_desc cwu50_cm3_desc = {
	.detect_panel_type = false,
};

static void cwu50_remove(struct mipi_dsi_device *dsi)
{
	struct cwu50 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id cwu50_of_match[] = {
	{ .compatible = "cw,cwu50", .data = &cwu50_desc },
	{ .compatible = "clockwork,cwu50-cm3", .data = &cwu50_cm3_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, cwu50_of_match);

static struct mipi_dsi_driver cwu50_driver = {
	.probe = cwu50_probe,
	.remove = cwu50_remove,
	.driver = {
		.name = "panel-cwu50",
		.of_match_table = cwu50_of_match,
	},
};
module_mipi_dsi_driver(cwu50_driver);

MODULE_DESCRIPTION("DRM Driver for cwu50 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
