#include "MagicFormats.h"

inline unsigned char rgbToLuminance(unsigned char r, unsigned char g, unsigned char b)
{
	unsigned int colorSumm = (r + g + b);

	return (colorSumm / 3);
}

class FormatA4L4 : public MagicFormat
{
	D3DFORMAT GetD3DFormat(void) const override
	{
		return D3DFMT_A4L4;
	}

	const char* GetFormatName(void) const override
	{
		return "A4L4";
	}

	struct pixel_t
	{
		unsigned char lum : 4;
		unsigned char alpha : 4;
	};

	size_t GetFormatTextureDataSize(unsigned int width, unsigned int height) const override
	{
		return width * height;
	}

	void GetTextureRWFormat(MAGIC_RASTER_FORMAT& rasterFormatOut, unsigned int& depthOut, MAGIC_COLOR_ORDERING& colorOrderOut) const
	{
		rasterFormatOut = RASTER_8888;
		depthOut = 32;
		colorOrderOut = COLOR_BGRA;
	}

	void ConvertToRW(const void *texData, unsigned int texMipWidth, unsigned int texMipHeight, size_t texDataSize, void *texOut) const override
	{
		unsigned int texItemCount = (texMipWidth * texMipHeight);
		const pixel_t *encodedColors = (pixel_t*)texData;
		for (unsigned int n = 0; n < texItemCount; n++)
		{
			const pixel_t *theTexel = encodedColors + n;
			unsigned char lum = theTexel->lum * 17;
			MagicPutTexelRGBA(texOut, n, RASTER_8888, 32, COLOR_BGRA, lum, lum, lum, theTexel->alpha * 17);
		}
	}

	void ConvertFromRW(unsigned int texMipWidth, unsigned int texMipHeight, const void *texelSource, MAGIC_RASTER_FORMAT rasterFormat,
		unsigned int depth, MAGIC_COLOR_ORDERING colorOrder, MAGIC_PALETTE_TYPE paletteType, const void *paletteData, unsigned int paletteSize,
		void *texOut) const override
	{
		unsigned int texItemCount = (texMipWidth * texMipHeight);
		pixel_t *encodedColors = (pixel_t*)texOut;
		for (unsigned int n = 0; n < texItemCount; n++)
		{
			unsigned char r, g, b, a;
			MagicBrowseTexelRGBA(texelSource, n,rasterFormat, depth, colorOrder, paletteType, paletteData, paletteSize, r, g, b, a);
			unsigned char lumVal = rgbToLuminance(r, g, b);
			pixel_t *theTexel = (encodedColors + n);
			theTexel->lum = lumVal / 17;
			theTexel->alpha = a / 17;
		}
	}
};

static FormatA4L4 a4l4Format;

MAGICAPI MagicFormat * __MAGICCALL GetFormatInstance()
{
	return &a4l4Format;
}