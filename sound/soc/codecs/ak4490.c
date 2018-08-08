/*
 * AK4490 ASoC codec driver
 *
 * Copyright (c) NS Technology Research 2018
 *
 *	 Naoki Serizawa <platunus70@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include "ak4490.h"

struct ak4490_private {
	struct regmap *regmap;
	unsigned int format;
	unsigned int rate;
};

static inline int get_mode_reg(int rate, int width)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mode_reg); i++) {
		if (mode_reg[i].rate == rate && mode_reg[i].width == width)
			return i;
	}
	return -1;
}

static const struct reg_default ak4490_reg_defaults[] = {
	{ AK4490_REG_CONTROL1,		0x04 },
	{ AK4490_REG_CONTROL2,		0x22 },
	{ AK4490_REG_CONTROL3,		0x00 },
	{ AK4490_REG_ATTL,			0xFF },
	{ AK4490_REG_ATTR,			0xFF },
	{ AK4490_REG_CONTROL4,		0x00 },
	{ AK4490_REG_CONTROL5,		0x00 },
	{ AK4490_REG_CONTROL6,		0x00 },
	{ AK4490_REG_CONTROL7,		0x00 },
	{ AK4490_REG_CONTROL8,		0x00 },
	{ AK4490_REG_EXT_CLKGEN,	0x88 }, // 10001000: 44.1kHz/1x/32bit/ClockOut
};

static bool ak4490_readable_reg(struct device *dev, unsigned int reg)
{
	return (reg <= AK4490_REG_CONTROL8);
}

static bool ak4490_writeable_reg(struct device *dev, unsigned int reg)
{
	return (reg <= AK4490_REG_EXT_CLKGEN);
}

static bool ak4490_volatile_reg(struct device *dev, unsigned int reg)
{
	return false;
}

static int ak4490_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_write(priv->regmap, AK4490_REG_CONTROL1,
			AK4490_ACKS_MANUAL | AK4490_DIF_I2S32 | AK4490_RSTN_RESET);
		regmap_write(priv->regmap, AK4490_REG_EXT_CLKGEN,
			AK4490_CLK_ENABLE);
		regmap_update_bits(priv->regmap, AK4490_REG_CONTROL1, 
			AK4490_RSTN, AK4490_RSTN_NORMAL);
		break;

	case SND_SOC_BIAS_OFF:
		regmap_update_bits(priv->regmap, AK4490_REG_CONTROL1, 
			AK4490_RSTN, AK4490_RSTN_RESET);
		regmap_update_bits(priv->regmap, AK4490_REG_EXT_CLKGEN,
			AK4490_CLK, AK4490_CLK_DISABLE);
		break;
	}
	return 0;
}

static int ak4490_set_dai_fmt(struct snd_soc_dai *codec_dai,
							 unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);

	priv->format = format;

	/* clock inversion */
	if ((format & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
		return -EINVAL;
	}

	/* set master/slave audio interface */
	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBM_CFM ) {
//	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFM ) {
		return -EINVAL;
	}

	return 0;
}

static int ak4490_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL2,
				 AK4490_SMUTE, !!mute);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak4490_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);
	int ret;

	unsigned int reg;
	int i, rate, width;

	priv->rate = params_rate(params);

	rate = params_rate(params);
	width = params_width(params);

	i = get_mode_reg(rate, width);
	if (i == -1) {
		printk(KERN_ERR
			"Invalid parameters: ak4490_hw_params: rate:%d, width:%d\n",
			rate, width);
		return -EINVAL;
	}

	ret = regmap_write(priv->regmap, AK4490_REG_CONTROL1, 
		AK4490_ACKS_MANUAL | mode_reg[i].dif | AK4490_RSTN_RESET);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL1\n");
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL2, 
		AK4490_DFSL, mode_reg[i].dfsl);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL2\n");
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL4, 
		AK4490_DFSH, mode_reg[i].dfsh);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL4\n");
		return ret;
	}

	ret = regmap_write(priv->regmap, AK4490_REG_EXT_CLKGEN, 
		AK4490_CLK_ENABLE | mode_reg[i].clkgen);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_EXT_CLKGEN\n");
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL1, 
		AK4490_RSTN, AK4490_RSTN_NORMAL);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL1\n");
		return ret;
	}

	regmap_read(priv->regmap, AK4490_REG_EXT_CLKGEN, &reg);
	printk(KERN_INFO "ak4490_hw_params: rate:%d width:%d reg:%02X\n",
			rate, width, reg);

	return 0;
}

static const struct snd_soc_dai_ops ak4490_dai_ops = {
	.set_fmt	= ak4490_set_dai_fmt,
	.hw_params	= ak4490_hw_params,
	.digital_mute	= ak4490_digital_mute,
};

static const char * const ak4490_dsp_slow_texts[] = {
	"Sharp",
	"Slow",
};

static const char * const ak4490_dsp_delay_texts[] = {
	"Traditional",
	"Short Delay",
};

static const char * const ak4490_dsp_sslow_texts[] = {
	"Normal",
	"Super Slow",
};

static const unsigned int ak4490_dsp_slow_values[] = {
	0,
	1,
};

static const unsigned int ak4490_dsp_delay_values[] = {
	0,
	1,
};

static const unsigned int ak4490_dsp_sslow_values[] = {
	0,
	1,
};


static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_dsp_delay,
			AK4490_REG_CONTROL2, AK4490_SD_SHIFT, 0b1,
			ak4490_dsp_delay_texts,
			ak4490_dsp_delay_values);

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_dsp_slow,
			AK4490_REG_CONTROL3, AK4490_SLOW_SHIFT, 0b1,
			ak4490_dsp_slow_texts,
			ak4490_dsp_slow_values);

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_dsp_sslow,
			AK4490_REG_CONTROL4, AK4490_SSLOW_SHIFT, 0b1,
			ak4490_dsp_sslow_texts,
			ak4490_dsp_sslow_values);

static const char * const ak4490_deemphasis_filter_texts[] = {
	"44.1kHz",
	"Off",
	"48kHz",
	"32kHz",
};

static const unsigned int ak4490_deemphasis_filter_values[] = {
	0,
	1,
	2,
	3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_deemphasis_filter,
			AK4490_REG_CONTROL2, AK4490_DEM_SHIFT, 0b11,
			ak4490_deemphasis_filter_texts,
			ak4490_deemphasis_filter_values);

static const char * const ak4490_sound_setting_texts[] = {
	"1",
	"2",
	"3",
};

static const unsigned int ak4490_sound_setting_values[] = {
	0,
	1,
	2,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_sound_setting,
			AK4490_REG_CONTROL7, AK4490_SC_SHIFT, 0b11,
			ak4490_sound_setting_texts,
			ak4490_sound_setting_values);

static const struct snd_kcontrol_new ak4490_controls[] = {
	SOC_ENUM("De-emphasis", ak4490_deemphasis_filter),
	SOC_ENUM("Roll-off (Delay)", ak4490_dsp_delay),
	SOC_ENUM("Roll-off (Slow)", ak4490_dsp_slow),
	SOC_ENUM("Roll-off (Super Slow)", ak4490_dsp_sslow),
	SOC_ENUM("Sound", ak4490_sound_setting),
};

static const struct snd_soc_dapm_widget ak4490_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("IOUTL+"),
SND_SOC_DAPM_OUTPUT("IOUTL-"),
SND_SOC_DAPM_OUTPUT("IOUTR+"),
SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route ak4490_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

#define AK4490_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver ak4490_dai = {
	.name = "ak4490-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 44100,
		.rate_max = 192000,
		.formats = AK4490_FORMATS, },
	.ops = &ak4490_dai_ops,
};

static const struct snd_soc_codec_driver soc_codec_dev_ak4490 = {
	.set_bias_level = ak4490_set_bias_level,
	.suspend_bias_off = true,

	.component_driver = {
		.controls			= ak4490_controls,
		.num_controls		= ARRAY_SIZE(ak4490_controls),
		.dapm_widgets		= ak4490_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(ak4490_dapm_widgets),
		.dapm_routes		= ak4490_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(ak4490_dapm_routes),
	},
};

static const struct of_device_id ak4490_of_match[] = {
	{ .compatible = "akm,ak4490", },
	{ }
};
MODULE_DEVICE_TABLE(of, ak4490_of_match);

const struct regmap_config ak4490_regmap_config = {
	.reg_bits			= AK4490_REG_BITS,
	.val_bits			= AK4490_VAL_BITS,
	.max_register		= AK4490_REG_EXT_CLKGEN,

	.reg_defaults		= ak4490_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(ak4490_reg_defaults),
	.writeable_reg		= ak4490_writeable_reg,
	.readable_reg		= ak4490_readable_reg,
	.volatile_reg		= ak4490_volatile_reg,
	.cache_type			= REGCACHE_RBTREE,
};

static int ak4490_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct ak4490_private *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(struct ak4490_private),
				  GFP_KERNEL);
	if (priv == NULL) 
	{
		printk(KERN_ERR "ak4490_i2c_probe: devm_kzalloc\n");
		return -ENOMEM;
	}

	priv->regmap = devm_regmap_init_i2c(client, &ak4490_regmap_config);
	if (IS_ERR(priv->regmap))
	{
		printk(KERN_ERR "ak4490_i2c_probe: devm_regmap_init_i2c\n");
		return PTR_ERR(priv->regmap);
	}

	i2c_set_clientdata(client, priv);

	ret =  snd_soc_register_codec(&client->dev,
			&soc_codec_dev_ak4490, &ak4490_dai, 1);

	return ret;
}

static int ak4490_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id ak4490_i2c_id[] = {
	{ "ak4490", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4490_i2c_id);

static struct i2c_driver ak4490_i2c_driver = {
	.driver = {
		.name = "ak4490",
		.of_match_table = ak4490_of_match,
	},
	.probe =	ak4490_i2c_probe,
	.remove =   ak4490_i2c_remove,
	.id_table = ak4490_i2c_id,
};

static int __init ak4490_modinit(void)
{
	int ret;

	ret = i2c_add_driver(&ak4490_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register AK4490 I2C driver: %d\n",
			   ret);
	}
	return 0;
}
module_init(ak4490_modinit);

static void __exit ak4490_exit(void)
{
	i2c_del_driver(&ak4490_i2c_driver);
}
module_exit(ak4490_exit);

MODULE_DESCRIPTION("ASoC AK4490 driver");
MODULE_AUTHOR("Naoki Serizawa <platunus70@gmail.com>");
MODULE_LICENSE("GPL");
