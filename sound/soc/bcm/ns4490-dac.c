/*
 * ASoC Driver for NS NS4490 DAC.
 *
 * Author:	Naoki Serizawa <platunus70@gmail.com>
 *		Copyright 2018
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "../codecs/ak4490.h"

static struct gpio_desc *ak4490_pdn;

static int snd_ns_ns4490_dac_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static inline int get_mode_reg(int rate, int width)
{
	int i;  

	for (i = 0; i < ARRAY_SIZE(mode_reg); i++) {
		if (mode_reg[i].rate == rate && mode_reg[i].width == width)
			return i;
	}
	return -1;
}

static int snd_ns_ns4490_dac_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

    int i;

	i = get_mode_reg(params_rate(params), params_width(params));
	if (i == -1) return -1;

    return snd_soc_dai_set_bclk_ratio(cpu_dai, mode_reg[i].bclk_ratio);
}

/* machine stream operations */
static struct snd_soc_ops snd_ns_ns4490_dac_ops = {
	.hw_params = snd_ns_ns4490_dac_hw_params,
};

static struct snd_soc_dai_link snd_ns_ns4490_dac_dai[] = {
{
	.name		= "NS4490-DAC",
	.stream_name	= "NS4490-DAC HiFi",
	.cpu_dai_name	= "bcm2708-i2s.0",
	.codec_dai_name	= "ak4490-hifi",
	.platform_name	= "bcm2708-i2s.0",
	.codec_name	= "ak4490.1-0010",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM,
	.ops		= &snd_ns_ns4490_dac_ops,
	.init		= snd_ns_ns4490_dac_init,
},
};

/* audio machine driver */
static struct snd_soc_card snd_ns_ns4490_dac = {
	.name         = "snd_ns_ns4490_dac",
	.owner        = THIS_MODULE,
	.dai_link     = snd_ns_ns4490_dac_dai,
	.num_links    = ARRAY_SIZE(snd_ns_ns4490_dac_dai),
};

static int snd_ns_ns4490_dac_probe(struct platform_device *pdev)
{
	int ret = 0;

	snd_ns_ns4490_dac.dev = &pdev->dev;
	
	if (pdev->dev.of_node) {
		struct device_node *i2s_node;
		struct snd_soc_dai_link *dai = &snd_ns_ns4490_dac_dai[0];
		i2s_node = of_parse_phandle(pdev->dev.of_node, "i2s-controller", 0);

		if (i2s_node) {
			dai->cpu_dai_name = NULL;
			dai->cpu_of_node = i2s_node;
			dai->platform_name = NULL;
			dai->platform_of_node = i2s_node;
		}

	}
	
	ret = snd_soc_register_card(&snd_ns_ns4490_dac);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n", ret);
		return ret;
	}

    ak4490_pdn = gpiod_get_index(&pdev->dev, "pdn", 0, GPIOD_ASIS);
	if (ak4490_pdn) {
		dev_err(&pdev->dev, "gpiod_get_index() failed\n");
		return -1;
	}

    ret = gpiod_direction_output(ak4490_pdn, 1);
	if (!ret) {
		dev_err(&pdev->dev, "gpiod_direction_output() failed: %d\n", ret);
		return -1;
	}

    /* Reset the slave. */
    gpiod_set_value(ak4490_pdn, false);
    mdelay(1);
    gpiod_set_value(ak4490_pdn, true);

	return ret;
}

static int snd_ns_ns4490_dac_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&snd_ns_ns4490_dac);
}

static const struct of_device_id snd_ns_ns4490_dac_of_match[] = {
	{ .compatible = "ns,ns4490-dac", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_ns_ns4490_dac_of_match);

static struct platform_driver snd_ns_ns4490_dac_driver = {
        .driver = {
                .name   = "snd-ns4490-dac",
                .owner  = THIS_MODULE,
                .of_match_table = snd_ns_ns4490_dac_of_match,
        },
        .probe          = snd_ns_ns4490_dac_probe,
        .remove         = snd_ns_ns4490_dac_remove,
};

module_platform_driver(snd_ns_ns4490_dac_driver);

MODULE_AUTHOR("Naoki Serizawa <platunu70@gmail.com>");
MODULE_DESCRIPTION("ASoC Driver for NS NS4490 DAC");
MODULE_LICENSE("GPL v2");
