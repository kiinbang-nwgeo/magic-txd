#include "StdInc.h"

#include "pixelformat.hxx"

#include "pixelutil.hxx"

#include "txdread.d3d.dxt.hxx"

#include "txdread.palette.hxx"

namespace rw
{

typedef rw::uint32 max_depth_item_type;

bool genericDecompressDXTNative(
    Interface *engineInterface, pixelDataTraversal& pixelData, uint32 dxtType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder
)
{
    eDXTCompressionMethod dxtMethod = engineInterface->GetDXTRuntime();

    // We must have stand-alone pixel data.
    // Otherwise we could mess up pretty badly!
    assert( pixelData.isNewlyAllocated == true );

    // Decide which raster format to use.
    bool conversionSuccessful = true;

    size_t mipmapCount = pixelData.mipmaps.size();

	for (size_t i = 0; i < mipmapCount; i++)
    {
        pixelDataTraversal::mipmapResource& mipLayer = pixelData.mipmaps[ i ];

        void *texelData = mipLayer.texels;

        // Allocate the new texel array.
        uint32 texLayerWidth = mipLayer.layerWidth;
        uint32 texLayerHeight = mipLayer.layerHeight;

		void *newtexels;
		uint32 dataSize;

        // Get the compressed block count.
        uint32 texWidth = mipLayer.width;
        uint32 texHeight = mipLayer.height;

        bool successfullyDecompressed =
            decompressTexelsUsingDXT <endian::little_endian> (
                engineInterface, dxtType, dxtMethod,
                texWidth, texHeight, dstRowAlignment,
                texLayerWidth, texLayerHeight,
                texelData, dstRasterFormat, dstColorOrder, dstDepth,
                newtexels, dataSize
            );

        // If even one mipmap fails to decompress, abort.
        if ( !successfullyDecompressed )
        {
            assert( i == 0 );

            conversionSuccessful = false;
            break;
        }

        // Replace the texel data.
		engineInterface->PixelFree( texelData );

		mipLayer.texels = newtexels;
		mipLayer.dataSize = dataSize;

        // Normalize the dimensions.
        mipLayer.width = texLayerWidth;
        mipLayer.height = texLayerHeight;
	}

    if (conversionSuccessful)
    {
        // Update the depth.
        pixelData.depth = dstDepth;

        // Update the raster format.
        pixelData.rasterFormat = dstRasterFormat;

        // Update the color order.
        pixelData.colorOrder = dstColorOrder;

        // Update row alignment.
        pixelData.rowAlignment = dstRowAlignment;

        // We are not compressed anymore.
        pixelData.compressionType = RWCOMPRESS_NONE;
    }

    return conversionSuccessful;
}

void genericCompressDXTNative( Interface *engineInterface, pixelDataTraversal& pixelData, uint32 dxtType )
{
    // We must get data in raw format.
    if ( pixelData.compressionType != RWCOMPRESS_NONE )
    {
        throw RwException( "runtime fault: attempting to compress an already compressed texture" );
    }

    // We must have stand-alone pixel data.
    // Otherwise we could mess up pretty badly!
    assert( pixelData.isNewlyAllocated == true );

    // Compress it now.
    size_t mipmapCount = pixelData.mipmaps.size();

    uint32 itemDepth = pixelData.depth;
    uint32 rowAlignment = pixelData.rowAlignment;

    eRasterFormat rasterFormat = pixelData.rasterFormat;
    ePaletteType paletteType = pixelData.paletteType;
    eColorOrdering colorOrder = pixelData.colorOrder;

    uint32 maxpalette = pixelData.paletteSize;
    void *paletteData = pixelData.paletteData;

    for ( size_t n = 0; n < mipmapCount; n++ )
    {
        pixelDataTraversal::mipmapResource& mipLayer = pixelData.mipmaps[ n ];

        uint32 mipWidth = mipLayer.width;
        uint32 mipHeight = mipLayer.height;

        void *texelSource = mipLayer.texels;

        void *dxtArray = NULL;
        uint32 dxtDataSize = 0;

        // Create the new DXT array.
        uint32 realMipWidth, realMipHeight;

        compressTexelsUsingDXT <endian::little_endian> (
            engineInterface,
            dxtType, texelSource, mipWidth, mipHeight, rowAlignment,
            rasterFormat, paletteData, paletteType, maxpalette, colorOrder, itemDepth,
            dxtArray, dxtDataSize,
            realMipWidth, realMipHeight
        );

        // Delete the raw texels.
        engineInterface->PixelFree( texelSource );

        if ( mipWidth != realMipWidth )
        {
            mipLayer.width = realMipWidth;
        }

        if ( mipHeight != realMipHeight )
        {
            mipLayer.height = realMipHeight;
        }

        // Put in the new DXTn texels.
        mipLayer.texels = dxtArray;

        // Update fields.
        mipLayer.dataSize = dxtDataSize;
    }

    // We are finished compressing.
    // If we were palettized, unset that.
    if ( paletteType != PALETTE_NONE )
    {
        // Free the palette colors.
        engineInterface->PixelFree( paletteData );

        // Reset the fields.
        pixelData.paletteType = PALETTE_NONE;
        pixelData.paletteData = NULL;
        pixelData.paletteSize = 0;
    }

    // Set a virtual raster format.
    // This is what is done by the R* DXT output system.
    {
        uint32 newDepth = itemDepth;
        eRasterFormat virtualRasterFormat = RASTER_8888;

        if ( dxtType == 1 )
        {
            newDepth = 16;

            if ( pixelData.hasAlpha )
            {
                virtualRasterFormat = RASTER_1555;
            }
            else
            {
                virtualRasterFormat = RASTER_565;
            }
        }
        else if ( dxtType == 2 || dxtType == 3 || dxtType == 4 || dxtType == 5 )
        {
            newDepth = 16;

            virtualRasterFormat = RASTER_4444;
        }

        if ( rasterFormat != virtualRasterFormat )
        {
            pixelData.rasterFormat = virtualRasterFormat;
        }

        if ( newDepth != itemDepth )
        {
            pixelData.depth = newDepth;
        }
    }

    // We are now successfully compressed, so set the correct compression type.
    eCompressionType targetCompressionType = RWCOMPRESS_NONE;

    if ( dxtType == 1 )
    {
        targetCompressionType = RWCOMPRESS_DXT1;
    }
    else if ( dxtType == 2 )
    {
        targetCompressionType = RWCOMPRESS_DXT2;
    }
    else if ( dxtType == 3 )
    {
        targetCompressionType = RWCOMPRESS_DXT3;
    }
    else if ( dxtType == 4 )
    {
        targetCompressionType = RWCOMPRESS_DXT4;
    }
    else if ( dxtType == 5 )
    {
        targetCompressionType = RWCOMPRESS_DXT5;
    }
    else
    {
        throw RwException( "runtime fault: unknown compression type request in DXT compressor" );
    }

    pixelData.rowAlignment = 0; // we are not raw anymore, so we do not have a row alignment.
    pixelData.compressionType = targetCompressionType;
}

AINLINE void _copyPaletteDepth_internal(
    const void *srcTexels, void *dstTexels,
    uint32 srcTexelOffX, uint32 srcTexelOffY,
    uint32 dstTexelOffX, uint32 dstTexelOffY,
    uint32 texWidth, uint32 texHeight,
    uint32 texProcessWidth, uint32 texProcessHeight,
    ePaletteType srcPaletteType, ePaletteType dstPaletteType,
    uint32 paletteSize,
    uint32 srcDepth, uint32 dstDepth,
    uint32 srcRowSize, uint32 dstRowSize
)
{
    for ( uint32 row = 0; row < texProcessHeight; row++ )
    {
        const void *srcRow = getConstTexelDataRow( srcTexels, srcRowSize, row + srcTexelOffY );
        void *dstRow = getTexelDataRow( dstTexels, dstRowSize, row + dstTexelOffY );

        for ( uint32 col = 0; col < texProcessWidth; col++ )
        {
            copyPaletteItemGeneric(
                srcRow, dstRow,
                col + srcTexelOffX, srcDepth, srcPaletteType,
                col + dstTexelOffX, dstDepth, dstPaletteType,
                paletteSize
            );
        }
    }
}

void ConvertPaletteDepthEx(
    const void *srcTexels, void *dstTexels,
    uint32 srcTexelOffX, uint32 srcTexelOffY,
    uint32 dstTexelOffX, uint32 dstTexelOffY,
    uint32 texWidth, uint32 texHeight,
    uint32 texProcessWidth, uint32 texProcessHeight,
    ePaletteType srcPaletteType, ePaletteType dstPaletteType,
    uint32 paletteSize,
    uint32 srcDepth, uint32 dstDepth,
    uint32 srcRowAlignment, uint32 dstRowAlignment
)
{
    // Copy palette indice.
    uint32 srcRowSize = getRasterDataRowSize( texWidth, srcDepth, srcRowAlignment );
    uint32 dstRowSize = getRasterDataRowSize( texWidth, dstDepth, dstRowAlignment );

    _copyPaletteDepth_internal(
        srcTexels, dstTexels,
        srcTexelOffX, srcTexelOffY,
        dstTexelOffX, dstTexelOffY,
        texWidth, texHeight,
        texProcessWidth, texProcessHeight,
        srcPaletteType, dstPaletteType,
        paletteSize,
        srcDepth, dstDepth,
        srcRowSize, dstRowSize
    );
}

void ConvertPaletteDepth(
    const void *srcTexels, void *dstTexels,
    uint32 texWidth, uint32 texHeight,
    ePaletteType srcPaletteType, ePaletteType dstPaletteType,
    uint32 paletteSize,
    uint32 srcDepth, uint32 dstDepth,
    uint32 srcRowAlignment, uint32 dstRowAlignment
)
{
    ConvertPaletteDepthEx(
        srcTexels, dstTexels,
        0, 0,
        0, 0,
        texWidth, texHeight,
        texWidth, texHeight,
        srcPaletteType, dstPaletteType,
        paletteSize,
        srcDepth, dstDepth,
        srcRowAlignment, dstRowAlignment
    );
}

template <typename srcColorDispatcher, typename dstColorDispatcher>
inline void copyTexelData(
    const void *srcTexels, void *dstTexels,
    srcColorDispatcher& fetchDispatch, dstColorDispatcher& putDispatch,
    uint32 mipWidth, uint32 mipHeight,
    uint32 srcDepth, uint32 dstDepth,
    uint32 srcRowAlignment, uint32 dstRowAlignment
)
{
    uint32 srcRowSize = getRasterDataRowSize( mipWidth, srcDepth, srcRowAlignment );
    uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstDepth, dstRowAlignment );

    copyTexelDataEx(
        srcTexels, dstTexels,
        fetchDispatch, putDispatch,
        mipWidth, mipHeight,
        0, 0,
        0, 0,
        srcRowSize, dstRowSize
    );
}

// Very optimized routine that does not always allocate a new destination texel buffer because it
// would not be necessary.
void TransformMipmapLayer(
    Interface *engineInterface,
    uint32 surfWidth, uint32 surfHeight, uint32 layerWidth, uint32 layerHeight, void *srcTexels, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, const void *srcPaletteData, uint32 srcPaletteSize,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType,
    bool hasSurfaceRowFormatChanged,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    // Check whether we need to reallocate the texels.
    void *dstTexels = srcTexels;

    uint32 dstTexelsDataSize = srcDataSize;

    if ( hasConflictingAddressing(
             surfWidth,
             srcDepth, srcRowAlignment, srcPaletteType,
             dstDepth, dstRowAlignment, dstPaletteType
         )
        )
    {
        uint32 rowSize = getRasterDataRowSize( surfWidth, dstDepth, dstRowAlignment );

        dstTexelsDataSize = getRasterDataSizeByRowSize( rowSize, surfHeight );

        dstTexels = engineInterface->PixelAllocate( dstTexelsDataSize );
    }

    // Kappa.
    if ( hasSurfaceRowFormatChanged || srcTexels != dstTexels )
    {
        try
        {
            if ( dstPaletteType != PALETTE_NONE )
            {
                // Make sure we came from a palette.
                assert( srcPaletteType != PALETTE_NONE );

                // We only have work to do if the depth changed or there is an addressing mode conflict.
                if ( srcTexels != dstTexels )
                {
                    ConvertPaletteDepth(
                        srcTexels, dstTexels,
                        surfWidth, surfHeight,
                        srcPaletteType, dstPaletteType, srcPaletteSize,
                        srcDepth, dstDepth,
                        srcRowAlignment, dstRowAlignment
                    );
                }
            }
            else
            {
                // We always have to do work, but very often we are optimized.
                colorModelDispatcher fetchDispatch( srcRasterFormat, srcColorOrder, srcDepth, srcPaletteData, srcPaletteSize, srcPaletteType );
                colorModelDispatcher putDispatch( dstRasterFormat, dstColorOrder, dstDepth, NULL, 0, PALETTE_NONE );

                copyTexelData(
                    srcTexels, dstTexels,
                    fetchDispatch, putDispatch,
                    surfWidth, surfHeight,
                    srcDepth, dstDepth,
                    srcRowAlignment, dstRowAlignment
                );
            }
        }
        catch( ... )
        {
            engineInterface->PixelFree( dstTexels );

            throw;
        }
    }
    
    // Give data to the runtime.
    dstTexelsOut = dstTexels;
    dstDataSizeOut = dstTexelsDataSize;
}

bool ConvertMipmapLayerNative(
    Interface *engineInterface,
    uint32 mipWidth, uint32 mipHeight, uint32 layerWidth, uint32 layerHeight, void *srcTexels, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, const void *srcPaletteData, uint32 srcPaletteSize, eCompressionType srcCompressionType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType, const void *dstPaletteData, uint32 dstPaletteSize, eCompressionType dstCompressionType,
    bool copyAnyway,
    uint32& dstPlaneWidthOut, uint32& dstPlaneHeightOut,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    bool isMipLayerTexels = true;

    // Perform this like a pipeline with multiple stages.
    uint32 srcDXTType;
    uint32 dstDXTType;

    bool isSrcDXTType = IsDXTCompressionType( srcCompressionType, srcDXTType );
    bool isDstDXTType = IsDXTCompressionType( dstCompressionType, dstDXTType );

    bool requiresCompression = ( srcCompressionType != dstCompressionType );

    if ( requiresCompression )
    {
        if ( isSrcDXTType )
        {
            assert( srcPaletteType == PALETTE_NONE );

            // Decompress stuff.
            eDXTCompressionMethod dxtMethod = engineInterface->GetDXTRuntime();

            void *decompressedTexels;
            uint32 decompressedSize;

            bool success = decompressTexelsUsingDXT <endian::little_endian> (
                engineInterface, srcDXTType, dxtMethod,
                mipWidth, mipHeight, dstRowAlignment,
                layerWidth, layerHeight,
                srcTexels, dstRasterFormat, dstColorOrder, dstDepth,
                decompressedTexels, decompressedSize
            );

            assert( success == true );

            // Update with raw raster data.
            srcRasterFormat = dstRasterFormat;
            srcColorOrder = dstColorOrder;
            srcDepth = dstDepth;
            srcRowAlignment = dstRowAlignment;

            srcTexels = decompressedTexels;
            srcDataSize = decompressedSize;

            mipWidth = layerWidth;
            mipHeight = layerHeight;

            srcCompressionType = RWCOMPRESS_NONE;

            isMipLayerTexels = false;
        }
    }

    if ( srcCompressionType == RWCOMPRESS_NONE && dstCompressionType == RWCOMPRESS_NONE )
    {
        void *newtexels = NULL;
        uint32 dstDataSize = 0;

        if ( dstPaletteType == PALETTE_NONE )
        {
            uint32 srcRowSize = getRasterDataRowSize( mipWidth, srcDepth, srcRowAlignment );

            uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstDepth, dstRowAlignment );

            dstDataSize = getRasterDataSizeByRowSize( dstRowSize, mipHeight );

            newtexels = engineInterface->PixelAllocate( dstDataSize );

            if ( !newtexels )
            {
                throw RwException( "failed to allocate texel destination buffer in texel conversion" );
            }

            try
            {
                colorModelDispatcher fetchDispatch( srcRasterFormat, srcColorOrder, srcDepth, srcPaletteData, srcPaletteSize, srcPaletteType );
                colorModelDispatcher putDispatch( dstRasterFormat, dstColorOrder, dstDepth, NULL, 0, PALETTE_NONE );

                // Do the conversion.
                copyTexelDataEx(
                    srcTexels, newtexels,
                    fetchDispatch, putDispatch,
                    mipWidth, mipHeight,
                    0, 0,
                    0, 0,
                    srcRowSize, dstRowSize
                );
            }
            catch( ... )
            {
                engineInterface->PixelFree( newtexels );

                throw;
            }
        }
        else if ( srcPaletteType != PALETTE_NONE )
        {
            if ( srcPaletteData == dstPaletteData )
            {
                // Fix the indice, if necessary.
                uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstDepth, dstRowAlignment );

                dstDataSize = getRasterDataSizeByRowSize( dstRowSize, mipHeight );

                newtexels = engineInterface->PixelAllocate( dstDataSize );

                if ( !newtexels )
                {
                    throw RwException( "failed to allocate palette texel buffer in texel conversion" );
                }

                try
                {
                    // Convert the depth.
                    ConvertPaletteDepth(
                        srcTexels, newtexels,
                        mipWidth, mipHeight,
                        srcPaletteType, dstPaletteType, srcPaletteSize,
                        srcDepth, dstDepth,
                        srcRowAlignment, dstRowAlignment
                    );
                }
                catch( ... )
                {
                    engineInterface->PixelFree( newtexels );

                    throw;
                }
            }
            else
            {
                // Remap texels.
                RemapMipmapLayer(
                    engineInterface,
                    dstRasterFormat, dstColorOrder,
                    srcTexels, mipWidth, mipHeight,
                    srcRasterFormat, srcColorOrder, srcDepth, srcPaletteType, srcPaletteData, srcPaletteSize,
                    dstPaletteData, dstPaletteSize,
                    dstDepth, dstPaletteType,
                    srcRowAlignment, dstRowAlignment,
                    newtexels, dstDataSize
                );
            }
        }
        else
        {
            // There must be a destination palette already allocated, as we cannot create one.
            assert( dstPaletteData != NULL );

            // Remap.
            RemapMipmapLayer(
                engineInterface,
                dstRasterFormat, dstColorOrder,
                srcTexels, mipWidth, mipHeight,
                srcRasterFormat, srcColorOrder, srcDepth, srcPaletteType, srcPaletteData, srcPaletteSize,
                dstPaletteData, dstPaletteSize,
                dstDepth, dstPaletteType,
                srcRowAlignment, dstRowAlignment,
                newtexels, dstDataSize
            );
        }
        
        if ( newtexels != NULL )
        {
            if ( isMipLayerTexels == false )
            {
                // If we have temporary texels, remove them.
                engineInterface->PixelFree( srcTexels );
            }

            // Store the new texels.
            srcTexels = newtexels;
            srcDataSize = dstDataSize;

            // Update raster properties.
            srcRasterFormat = dstRasterFormat;
            srcColorOrder = dstColorOrder;
            srcDepth = dstDepth;
            srcRowAlignment = dstRowAlignment;

            isMipLayerTexels = false;
        }
    }

    if ( requiresCompression )
    {
        // Perform compression now.
        if ( isDstDXTType )
        {
            void *dstTexels;
            uint32 dstDataSize;

            uint32 newWidth, newHeight;

            try
            {
                compressTexelsUsingDXT <endian::little_endian> (
                    engineInterface,
                    dstDXTType, srcTexels, mipWidth, mipHeight, srcRowAlignment,
                    srcRasterFormat, srcPaletteData, srcPaletteType, srcPaletteSize, srcColorOrder, srcDepth,
                    dstTexels, dstDataSize,
                    newWidth, newHeight
                );
            }
            catch( ... )
            {
                // Must free temporary texels on error.
                if ( isMipLayerTexels == false )
                {
                    engineInterface->PixelFree( srcTexels );
                }

                throw;
            }

            // Delete old texels (if necessary).
            if ( isMipLayerTexels == false )
            {
                engineInterface->PixelFree( srcTexels );
            }

            // Update parameters.
            srcTexels = dstTexels;
            srcDataSize = dstDataSize;

            // Clear raster format (for safety).
            srcRasterFormat = RASTER_DEFAULT;
            srcColorOrder = COLOR_BGRA;
            srcDepth = 16;
            srcRowAlignment = 0;

            mipWidth = newWidth;
            mipHeight = newHeight;
            
            srcCompressionType = dstCompressionType;

            isMipLayerTexels = false;
        }
    }

    // Output parameters.
    if ( copyAnyway == true || isMipLayerTexels == false )
    {
        dstPlaneWidthOut = mipWidth;
        dstPlaneHeightOut = mipHeight;

        dstTexelsOut = srcTexels;
        dstDataSizeOut = srcDataSize;

        return true;
    }

    return false;
}

bool ConvertMipmapLayerEx(
    Interface *engineInterface,
    const pixelDataTraversal::mipmapResource& mipLayer,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, const void *srcPaletteData, uint32 srcPaletteSize, eCompressionType srcCompressionType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType, const void *dstPaletteData, uint32 dstPaletteSize, eCompressionType dstCompressionType,
    bool copyAnyway,
    uint32& dstPlaneWidthOut, uint32& dstPlaneHeightOut,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    uint32 mipWidth = mipLayer.width;
    uint32 mipHeight = mipLayer.height;

    uint32 layerWidth = mipLayer.layerWidth;
    uint32 layerHeight = mipLayer.layerHeight;

    void *srcTexels = mipLayer.texels;
    uint32 srcDataSize = mipLayer.dataSize;

    return
        ConvertMipmapLayerNative(
            engineInterface,
            mipWidth, mipHeight, layerWidth, layerHeight, srcTexels, srcDataSize,
            srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder, srcPaletteType, srcPaletteData, srcPaletteSize, srcCompressionType,
            dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder, dstPaletteType, dstPaletteData, dstPaletteSize, dstCompressionType,
            copyAnyway,
            dstPlaneWidthOut, dstPlaneHeightOut,
            dstTexelsOut, dstDataSizeOut
        );
}

void CopyTransformRawMipmapLayer(
    Interface *engineInterface,
    uint32 surfWidth, uint32 surfHeight, uint32 layerWidth, uint32 layerHeight, void *srcTexels, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, const void *srcPaletteData, uint32 srcPaletteSize,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    // Gotta be able to iterate through the source buffer.
    uint32 srcRowSize = getRasterDataRowSize( surfWidth, srcDepth, srcRowAlignment );

    // Allocate a new copy of the destination buffer.
    uint32 dstRowSize = getRasterDataRowSize( surfWidth, dstDepth, dstRowAlignment );

    uint32 dstTexelsDataSize = getRasterDataSizeByRowSize( dstRowSize, surfHeight );

    void *dstTexels = engineInterface->PixelAllocate( dstTexelsDataSize );

    if ( !dstTexels )
    {
        throw RwException( "failed to allocate destination surface for mipmap copy transformation" );
    }

    try
    {
        if ( dstPaletteType != PALETTE_NONE )
        {
            // Make sure we came from a palette.
            assert( srcPaletteType != PALETTE_NONE );

            // Convert stuff.
            _copyPaletteDepth_internal(
                srcTexels, dstTexels,
                0, 0,
                0, 0,
                surfWidth, surfHeight,
                surfWidth, surfHeight,
                srcPaletteType, dstPaletteType, srcPaletteSize,
                srcDepth, dstDepth,
                srcRowSize, dstRowSize
            );
        }
        else
        {
            colorModelDispatcher fetchDispatch( srcRasterFormat, srcColorOrder, srcDepth, srcPaletteData, srcPaletteSize, srcPaletteType );
            colorModelDispatcher putDispatch( dstRasterFormat, dstColorOrder, dstDepth, NULL, 0, PALETTE_NONE );

            copyTexelDataEx(
                srcTexels, dstTexels,
                fetchDispatch, putDispatch,
                surfWidth, surfHeight,
                0, 0,
                0, 0,
                srcRowSize, dstRowSize
            );
        }
    }
    catch( ... )
    {
        engineInterface->PixelFree( dstTexels );

        throw;
    }
    
    // Give data to the runtime.
    dstTexelsOut = dstTexels;
    dstDataSizeOut = dstTexelsDataSize;
}

// Function to simply readjust mipmap depth and row alignment.
void DepthCopyTransformMipmapLayer(
    Interface *engineInterface,
    uint32 layerWidth, uint32 layerHeight, void *srcTexels, uint32 srcDataSize,
    uint32 srcDepth, uint32 srcRowAlignment, eByteAddressingMode srcByteAddr,
    uint32 dstDepth, uint32 dstRowAlignment, eByteAddressingMode dstByteAddr,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    // Allocate a new destination buffer.
    uint32 dstRowSize = getRasterDataRowSize( layerWidth, dstDepth, dstRowAlignment );

    uint32 dstDataSize = getRasterDataSizeByRowSize( dstRowSize, layerHeight );

    void *dstTexels = engineInterface->PixelAllocate( dstDataSize );

    if ( !dstTexels )
    {
        throw RwException( "failed to allocate destination texel buffer for mipmap depth-alignment transformation" );
    }
    
    try
    {
        bool isRowStructureSame = ( srcDepth == dstDepth && srcByteAddr == dstByteAddr );

        uint32 srcRowSize = getRasterDataRowSize( layerWidth, srcDepth, srcRowAlignment );

        for ( uint32 row = 0; row < layerHeight; row++ )
        {
            const void *srcRow = getConstTexelDataRow( srcTexels, srcRowSize, row );
            void *dstRow = getTexelDataRow( dstTexels, dstRowSize, row );

            // We either align depth items or we just copy over the rows.
            // TODO: maybe add some cleanly clear at places where stuff doesnt matter.
            if ( isRowStructureSame )
            {
                size_t actualMoveAmount = std::min( srcRowSize, dstRowSize );

                memcpy( dstRow, srcRow, actualMoveAmount );
            }
            else
            {
                max_depth_item_type item;

                for ( uint32 col = 0; col < layerWidth; col++ )
                {
                    // Just move over depth items.
                    getDataByDepth( srcRow, srcDepth, col, srcByteAddr, item );

                    setDataByDepth( dstRow, dstDepth, col, dstByteAddr, item );
                }
            }
        }
    }
    catch( ... )
    {
        engineInterface->PixelFree( dstTexels );

        throw;
    }

    // Return stuff.
    dstTexelsOut = dstTexels;
    dstDataSizeOut = dstDataSize;
}

bool DecideBestDXTCompressionFormat(
    Interface *engineInterface,
    bool srcHasAlpha,
    bool supportsDXT1, bool supportsDXT2, bool supportsDXT3, bool supportsDXT4, bool supportsDXT5,
    float quality,
    eCompressionType& dstCompressionTypeOut
)
{
    // Decide which DXT format we need based on the wanted support.
    eCompressionType targetCompressionType = RWCOMPRESS_NONE;

    // We go from DXT5 to DXT1, only mentioning the actually supported formats.
    if ( srcHasAlpha )
    {
        // Do a smart decision based on the quality parameter.
        if ( targetCompressionType == RWCOMPRESS_NONE )
        {
            if ( supportsDXT5 && quality >= 1.0f )
            {
                targetCompressionType = RWCOMPRESS_DXT5;
            }
            else if ( supportsDXT3 && quality < 1.0f )
            {
                targetCompressionType = RWCOMPRESS_DXT3;
            }
        }

        if ( targetCompressionType == RWCOMPRESS_NONE )
        {
            if ( supportsDXT5 )
            {
                targetCompressionType = RWCOMPRESS_DXT5;
            }
        }

        // No compression to DXT4 yet.

        if ( targetCompressionType == RWCOMPRESS_NONE )
        {
            if ( supportsDXT3 )
            {
                targetCompressionType = RWCOMPRESS_DXT3;
            }
        }

        // No compression to DXT2 yet.
    }

    if ( targetCompressionType == RWCOMPRESS_NONE )
    {
        // Finally we just try DXT1.
        if ( supportsDXT1 )
        {
            // Take it or leave it!
            // Actually, DXT1 _does_ support alpha, but I do not recommend using it.
            // It is only for very desperate people.
            targetCompressionType = RWCOMPRESS_DXT1;
        }
    }

    // Alright, so if we decided for anything other than a raw raster, we may begin compression.
    bool compressionSuccess = false;

    if ( targetCompressionType != RWCOMPRESS_NONE )
    {
        dstCompressionTypeOut = targetCompressionType;

        compressionSuccess = true;
    }

    return compressionSuccess;
}

void ConvertPaletteData(
    const void *srcPaletteTexels, void *dstPaletteTexels,
    uint32 srcPaletteSize, uint32 dstPaletteSize,
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder, uint32 srcPalRasterDepth,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder, uint32 dstPalRasterDepth
)
{
    // Process valid colors.
    uint32 canProcessCount = std::min( srcPaletteSize, dstPaletteSize );

    colorModelDispatcher fetchDispatcher( srcRasterFormat, srcColorOrder, srcPalRasterDepth, NULL, 0, PALETTE_NONE );
    colorModelDispatcher putDispatcher( dstRasterFormat, dstColorOrder, dstPalRasterDepth, NULL, 0, PALETTE_NONE );

    for ( uint32 n = 0; n < canProcessCount; n++ )
    {
        abstractColorItem colorItem;

        fetchDispatcher.getColor( srcPaletteTexels, n, colorItem );

        putDispatcher.setColor( dstPaletteTexels, n, colorItem );
    }

    // Zero out any remainder.
    for ( uint32 n = canProcessCount; n < dstPaletteSize; n++ )
    {
        putDispatcher.clearColor( dstPaletteTexels, n );
    }
}

AINLINE void TransformPaletteData_native(
    Interface *engineInterface,
    void *srcPaletteData,
    uint32 srcPaletteSize, uint32 dstPaletteSize,
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder, uint32 srcPalRasterDepth,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder, uint32 dstPalRasterDepth,
    bool canTransformIntoSource,
    void*& dstPalDataOut,
    bool& hasColorTransformedOut
)
{
    bool needsNewPalBuf = haveToAllocateNewPaletteBuffer( srcPalRasterDepth, srcPaletteSize, dstPalRasterDepth, dstPaletteSize );
    bool requiresConversion = doPaletteBuffersNeedConversion( srcRasterFormat, srcColorOrder, dstRasterFormat, dstColorOrder );

    void *dstPaletteData = srcPaletteData;

    if ( needsNewPalBuf || requiresConversion && !canTransformIntoSource )
    {
        // Need a new buffer.
        uint32 dstPalDataSize = getPaletteDataSize( dstPaletteSize, dstPalRasterDepth );

        dstPaletteData = engineInterface->PixelAllocate( dstPalDataSize );

        if ( !dstPaletteData )
        {
            throw RwException( "failed to allocate palette destination buffer" );
        }
    }

    if ( dstPaletteData != srcPaletteData || requiresConversion )
    {
        assert( dstPaletteData != srcPaletteData || canTransformIntoSource );

        try
        {
            ConvertPaletteData(
                srcPaletteData, dstPaletteData,
                srcPaletteSize, dstPaletteSize,
                srcRasterFormat, srcColorOrder, srcPalRasterDepth,
                dstRasterFormat, dstColorOrder, dstPalRasterDepth
            );
        }
        catch( ... )
        {
            // Clean up on error, because we aint needing the buffer anymore.
            if ( dstPaletteData != srcPaletteData )
            {
                engineInterface->PixelFree( dstPaletteData );
            }

            throw;
        }
    }

    // Return stuff.
    dstPalDataOut = dstPaletteData;
    hasColorTransformedOut = requiresConversion;
}

void TransformPaletteDataEx(
    Interface *engineInterface,
    void *srcPaletteData,
    uint32 srcPaletteSize, uint32 dstPaletteSize,
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder, uint32 srcPalRasterDepth,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder, uint32 dstPalRasterDepth,
    bool canTransformIntoSource,
    void*& dstPalDataOut
)
{
    bool colorConv; // unused.

    TransformPaletteData_native(
        engineInterface,
        srcPaletteData,
        srcPaletteSize, dstPaletteSize,
        srcRasterFormat, srcColorOrder, srcPalRasterDepth,
        dstRasterFormat, dstColorOrder, dstPalRasterDepth,
        canTransformIntoSource,
        dstPalDataOut,
        colorConv
    );
}

void TransformPaletteData(
    Interface *engineInterface,
    void *srcPaletteData,
    uint32 srcPaletteSize, uint32 dstPaletteSize,
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder,
    bool canTransformIntoSource,
    void*& dstPalDataOut
)
{
    uint32 srcPalRasterDepth = Bitmap::getRasterFormatDepth( srcRasterFormat );
    uint32 dstPalRasterDepth = Bitmap::getRasterFormatDepth( dstRasterFormat );

    TransformPaletteDataEx(
        engineInterface,
        srcPaletteData,
        srcPaletteSize, dstPaletteSize,
        srcRasterFormat, srcColorOrder, srcPalRasterDepth,
        dstRasterFormat, dstColorOrder, dstPalRasterDepth,
        canTransformIntoSource,
        dstPalDataOut
    );
}

void CopyConvertPaletteData(
    Interface *engineInterface,
    const void *srcPaletteData,
    uint32 srcPaletteSize, uint32 dstPaletteSize,
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder,
    void*& dstPaletteDataOut
)
{
    uint32 srcPalRasterDepth = Bitmap::getRasterFormatDepth( srcRasterFormat );
    uint32 dstPalRasterDepth = Bitmap::getRasterFormatDepth( dstRasterFormat );

    // We always need a new buffer.
    // This function is mainly used in logic which either takes all mipmap layers or clones them.
    uint32 dstPalDataSize = getPaletteDataSize( dstPaletteSize, dstPalRasterDepth );

    void *dstPaletteData = engineInterface->PixelAllocate( dstPalDataSize );

    if ( !dstPaletteData )
    {
        throw RwException( "failed to allocate palette destination buffer" );
    }

    try
    {
        ConvertPaletteData(
            srcPaletteData, dstPaletteData,
            srcPaletteSize, dstPaletteSize,
            srcRasterFormat, srcColorOrder, srcPalRasterDepth,
            dstRasterFormat, dstColorOrder, dstPalRasterDepth
        );
    }
    catch( ... )
    {
        // Clean up on error, because we aint needing the buffer anymore.
        engineInterface->PixelFree( dstPaletteData );

        throw;
    }

    // Return stuff.
    dstPaletteDataOut = dstPaletteData;
}

inline bool isPaletteTypeBigger( ePaletteType left, ePaletteType right )
{
    if ( left != PALETTE_NONE && right != PALETTE_NONE )
    {
        if ( left == PALETTE_8BIT )
        {
            if ( right == PALETTE_4BIT || right == PALETTE_4BIT_LSB )
            {
                return true;
            }
        }
    }

    return false;
}

bool ConvertPixelData( Interface *engineInterface, pixelDataTraversal& pixelsToConvert, const pixelFormat pixFormat )
{
    // We must have stand-alone pixel data.
    // Otherwise we could mess up pretty badly!
    assert( pixelsToConvert.isNewlyAllocated == true );

    // Decide how to convert stuff.
    bool hasUpdated = false;

    eCompressionType srcCompressionType = pixelsToConvert.compressionType;
    eCompressionType dstCompressionType = pixFormat.compressionType;

    uint32 srcDXTType, dstDXTType;

    bool isSrcDXTCompressed = IsDXTCompressionType( srcCompressionType, srcDXTType );
    bool isDstDXTCompressed = IsDXTCompressionType( dstCompressionType, dstDXTType );

    bool shouldRecalculateAlpha = false;

    if ( isSrcDXTCompressed || isDstDXTCompressed )
    {
        // Check whether the compression status even changed.
        if ( srcCompressionType != dstCompressionType )
        {
            // If we were compressed, decompress.
            bool compressionSuccess = false;
            bool decompressionSuccess = false;

            if ( isSrcDXTCompressed )
            {
                decompressionSuccess =
                    genericDecompressDXTNative(
                        engineInterface, pixelsToConvert, srcDXTType,
                        pixFormat.rasterFormat, Bitmap::getRasterFormatDepth( pixFormat.rasterFormat ),
                        pixFormat.rowAlignment, pixFormat.colorOrder
                    );
            }
            else
            {
                // No decompression means we are always successful.
                decompressionSuccess = true;
            }

            if ( decompressionSuccess )
            {
                // If we have to compress, do it.
                if ( isDstDXTCompressed )
                {
                    genericCompressDXTNative( engineInterface, pixelsToConvert, dstDXTType );

                    // No way to fail compression, yet.
                    compressionSuccess = true;
                }
                else
                {
                    // If we do not want to compress the target, then we
                    // want to put it into a proper pixel format.
                    pixelFormat pixSubFormat;
                    pixSubFormat.rasterFormat = pixFormat.rasterFormat;
                    pixSubFormat.depth = pixFormat.depth;
                    pixSubFormat.rowAlignment = pixFormat.rowAlignment;
                    pixSubFormat.colorOrder = pixFormat.colorOrder;
                    pixSubFormat.paletteType = pixFormat.paletteType;
                    pixSubFormat.compressionType = RWCOMPRESS_NONE;

                    bool subSuccess = ConvertPixelData( engineInterface, pixelsToConvert, pixSubFormat );

                    // We are successful if the sub conversion succeeded.
                    compressionSuccess = subSuccess;
                }
            }

            if ( compressionSuccess || decompressionSuccess )
            {
                // TODO: if this routine is incomplete, revert to last known best status.
                hasUpdated = true;
            }
        }
    }
    else if ( srcCompressionType == RWCOMPRESS_NONE && dstCompressionType == RWCOMPRESS_NONE )
    {
        ePaletteType srcPaletteType = pixelsToConvert.paletteType;
        ePaletteType dstPaletteType = pixFormat.paletteType;

        if ( ( srcPaletteType == PALETTE_NONE || isPaletteTypeBigger( srcPaletteType, dstPaletteType ) ) && dstPaletteType != PALETTE_NONE )
        {
            // Call into the palettizer.
            // It will do the remainder of the complex job.
            PalettizePixelData( engineInterface, pixelsToConvert, pixFormat );

            // We assume the palettization has always succeeded.
            hasUpdated = true;

            // Recalculating alpha is important.
            shouldRecalculateAlpha = true;
        }
        else if ( srcPaletteType != PALETTE_NONE && dstPaletteType == PALETTE_NONE ||
                  srcPaletteType != PALETTE_NONE && dstPaletteType != PALETTE_NONE ||
                  srcPaletteType == PALETTE_NONE && dstPaletteType == PALETTE_NONE )
        {
            // Grab the values on the stack.
            eRasterFormat srcRasterFormat = pixelsToConvert.rasterFormat;
            eColorOrdering srcColorOrder = pixelsToConvert.colorOrder;
            uint32 srcDepth = pixelsToConvert.depth;
            uint32 srcRowAlignment = pixelsToConvert.rowAlignment;

            eRasterFormat dstRasterFormat = pixFormat.rasterFormat;
            eColorOrdering dstColorOrder = pixFormat.colorOrder;
            uint32 dstDepth = pixFormat.depth;
            uint32 dstRowAlignment = pixFormat.rowAlignment;

            // Check whether we even have to update the texels.
            // That is if any of the properties that do anything have changed.
            if ( srcRasterFormat != dstRasterFormat || srcPaletteType != dstPaletteType || srcColorOrder != dstColorOrder || srcDepth != dstDepth || srcRowAlignment != dstRowAlignment )
            {
                bool didTransformColorData = false;

                // Grab palette parameters.
                void *srcPaletteTexels = pixelsToConvert.paletteData;
                uint32 srcPaletteSize = pixelsToConvert.paletteSize;

                void *dstPaletteTexels = srcPaletteTexels;
                uint32 dstPaletteSize = srcPaletteSize;

                // Check whether we have to update the palette texels.
                if ( dstPaletteType != PALETTE_NONE )
                {
                    // Make sure we had a palette before.
                    assert( srcPaletteType != PALETTE_NONE );

                    dstPaletteSize = getPaletteItemCount( dstPaletteType );

                    uint32 srcPalRasterDepth = Bitmap::getRasterFormatDepth( srcRasterFormat );
                    uint32 dstPalRasterDepth = Bitmap::getRasterFormatDepth( dstRasterFormat );

                    bool didColorConvert;

                    TransformPaletteData_native(
                        engineInterface,
                        srcPaletteTexels,
                        srcPaletteSize, dstPaletteSize,
                        srcRasterFormat, srcColorOrder, srcPalRasterDepth,
                        dstRasterFormat, dstColorOrder, dstPalRasterDepth,
                        true,
                        dstPaletteTexels,
                        didColorConvert
                    );

                    if ( didColorConvert )
                    {
                        didTransformColorData = true;
                    }
                }
                else
                {
                    dstPaletteTexels = NULL;
                    dstPaletteSize = 0;
                }

                try
                {
                    // Process the mipmaps if they actually change.
                    // This is just one of the required properties, not a shortcut.
                    bool hasSurfaceBufferFormatChanged =
                        doRawMipmapBuffersNeedConversion(
                            srcRasterFormat, srcDepth, srcColorOrder, srcPaletteType,
                            dstRasterFormat, dstDepth, dstColorOrder, dstPaletteType
                        );

                    // Process mipmaps.
                    size_t mipmapCount = pixelsToConvert.mipmaps.size();

                    // Determine the depth of the items.
                    for ( size_t n = 0; n < mipmapCount; n++ )
                    {
                        pixelDataTraversal::mipmapResource& mipLayer = pixelsToConvert.mipmaps[ n ];

                        // Get source parameters.
                        uint32 surfWidth = mipLayer.width;
                        uint32 surfHeight = mipLayer.height;

                        uint32 layerWidth = mipLayer.layerWidth;
                        uint32 layerHeight = mipLayer.layerHeight;

                        void *srcTexels = mipLayer.texels;
                        uint32 srcDataSize = mipLayer.dataSize;

                        // Convert this mipmap.
                        // This routine is optimized to only return a new texel buffer if absolutely required.
                        void *dstTexels;
                        uint32 dstTexelsDataSize;

                        TransformMipmapLayer(
                            engineInterface,
                            surfWidth, surfHeight, layerWidth, layerHeight, srcTexels, srcDataSize,
                            srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder, srcPaletteType, srcPaletteTexels, srcPaletteSize,
                            dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder, dstPaletteType,
                            hasSurfaceBufferFormatChanged,
                            dstTexels, dstTexelsDataSize
                        );

                        // Update mipmap properties.
                        if ( dstTexels != srcTexels )
                        {
                            // Delete old texels.
                            // We always have texels allocated.
                            engineInterface->PixelFree( srcTexels );

                            // Replace stuff.
                            mipLayer.texels = dstTexels;
                        }

                        if ( dstTexelsDataSize != srcDataSize )
                        {
                            // Update the data size.
                            mipLayer.dataSize = dstTexelsDataSize;
                        }
                    }

                    if ( hasSurfaceBufferFormatChanged )
                    {
                        didTransformColorData = true;
                    }
                }
                catch( ... )
                {
                    // Clean up after any palette transformation on error.
                    if ( dstPaletteTexels != NULL )
                    {
                        if ( dstPaletteTexels != srcPaletteTexels )
                        {
                            engineInterface->PixelFree( dstPaletteTexels );
                        }
                    }

                    throw;
                }

                // Update the palette if necessary.
                if ( srcPaletteTexels != dstPaletteTexels )
                {
                    if ( srcPaletteTexels )
                    {
                        // Delete old palette texels.
                        engineInterface->PixelFree( srcPaletteTexels );
                    }

                    // Put in the new ones.
                    pixelsToConvert.paletteData = dstPaletteTexels;
                }

                if ( srcPaletteSize != dstPaletteSize )
                {
                    pixelsToConvert.paletteSize = dstPaletteSize;
                }

                // Update meta properties, because format has changed.
                hasUpdated = true;

                if ( didTransformColorData )
                {
                    // We definately should recalculate alpha.
                    shouldRecalculateAlpha = true;
                }
            }

            // Update texture properties that changed.
            if ( srcRasterFormat != dstRasterFormat )
            {
                pixelsToConvert.rasterFormat = dstRasterFormat;
            }

            if ( srcPaletteType != dstPaletteType )
            {
                pixelsToConvert.paletteType = dstPaletteType;
            }

            if ( srcColorOrder != dstColorOrder )
            {
                pixelsToConvert.colorOrder = dstColorOrder;
            }

            if ( srcDepth != dstDepth )
            {
                pixelsToConvert.depth = dstDepth;
            }
            
            if ( srcRowAlignment != dstRowAlignment )
            {
                pixelsToConvert.rowAlignment = dstRowAlignment;
            }
        }
    }

    // If we have updated the pixels, we _should_ recalculate the pixel alpha flag.
    if ( hasUpdated )
    {
        if ( shouldRecalculateAlpha )
        {
            pixelsToConvert.hasAlpha = calculateHasAlpha( pixelsToConvert );
        }
    }

    return hasUpdated;
}

bool ConvertPixelDataDeferred( Interface *engineInterface, const pixelDataTraversal& srcPixels, pixelDataTraversal& dstPixels, const pixelFormat pixFormat )
{
    // First create a new copy of the texels.
    dstPixels.CloneFrom( engineInterface, srcPixels );

    // Perform the conversion on the new texels.
    return ConvertPixelData( engineInterface, dstPixels, pixFormat );
}

// Pixel manipulation API.
bool BrowseTexelRGBA(
    const void *texelSource, uint32 texelIndex,
    eRasterFormat rasterFormat, uint32 depth, eColorOrdering colorOrder, ePaletteType paletteType, const void *paletteData, uint32 paletteSize,
    uint8& redOut, uint8& greenOut, uint8& blueOut, uint8& alphaOut
)
{
    return colorModelDispatcher ( rasterFormat, colorOrder, depth, paletteData, paletteSize, paletteType ).getRGBA( texelSource, texelIndex, redOut, greenOut, blueOut, alphaOut );
}

bool BrowseTexelLuminance(
    const void *texelSource, uint32 texelIndex,
    eRasterFormat rasterFormat, uint32 depth, ePaletteType paletteType, const void *paletteData, uint32 paletteSize,
    uint8& lumOut, uint8& alphaOut
)
{
    return colorModelDispatcher ( rasterFormat, COLOR_RGBA, depth, paletteData, paletteSize, paletteType ).getLuminance( texelSource, texelIndex, lumOut, alphaOut );
}

eColorModel GetRasterFormatColorModel( eRasterFormat rasterFormat )
{
    return getColorModelFromRasterFormat( rasterFormat );
}

bool PutTexelRGBA(
    void *texelSource, uint32 texelIndex,
    eRasterFormat rasterFormat, uint32 depth, eColorOrdering colorOrder,
    uint8 red, uint8 green, uint8 blue, uint8 alpha
)
{
    return colorModelDispatcher ( rasterFormat, colorOrder, depth, NULL, 0, PALETTE_NONE ).setRGBA( texelSource, texelIndex, red, green, blue, alpha );
}

bool PutTexelLuminance(
    void *texelSource, uint32 texelIndex,
    eRasterFormat rasterFormat, uint32 depth,
    uint8 lum, uint8 alpha
)
{
    return colorModelDispatcher ( rasterFormat, COLOR_RGBA, depth, NULL, 0, PALETTE_NONE ).setLuminance( texelSource, texelIndex, lum, alpha );
}

void pixelDataTraversal::FreeMipmap( Interface *engineInterface, mipmapResource& mipData )
{
    // Free allocatable data.
    if ( void *texels = mipData.texels )
    {
        engineInterface->PixelFree( texels );

        mipData.texels = NULL;
    }
}

void pixelDataTraversal::FreePixels( Interface *engineInterface )
{
    if ( this->isNewlyAllocated )
    {
        size_t mipmapCount = this->mipmaps.size();

        for ( size_t n = 0; n < mipmapCount; n++ )
        {
            mipmapResource& thisLayer = this->mipmaps[ n ];

            FreeMipmap( engineInterface, thisLayer );
        }

        this->mipmaps.clear();

        if ( void *paletteData = this->paletteData )
        {
            engineInterface->PixelFree( paletteData );

            this->paletteData = NULL;
        }

        this->isNewlyAllocated = false;
    }
}

void pixelDataTraversal::CloneFrom( Interface *engineInterface, const pixelDataTraversal& right )
{
    // Free any previous data.
    this->FreePixels( engineInterface );

    // Clone parameters.
    eRasterFormat rasterFormat = right.rasterFormat;

    this->isNewlyAllocated = true;  // since we clone.
    this->rasterFormat = rasterFormat;
    this->depth = right.depth;
    this->rowAlignment = right.rowAlignment;
    this->colorOrder = right.colorOrder;

    // Clone palette.
    {
        this->paletteType = right.paletteType;
    
        void *srcPaletteData = right.paletteData;
        void *dstPaletteData = NULL;

        uint32 dstPaletteSize = 0;

        if ( srcPaletteData )
        {
            uint32 srcPaletteSize = right.paletteSize;  
        
            // Copy the palette texels.
            uint32 palRasterDepth = Bitmap::getRasterFormatDepth( rasterFormat );

            uint32 palDataSize = getPaletteDataSize( srcPaletteSize, palRasterDepth );

            dstPaletteData = engineInterface->PixelAllocate( palDataSize );

            if ( !dstPaletteData )
            {
                throw RwException( "failed to allocate palette buffer in pixel container cloning routine" );
            }

            memcpy( dstPaletteData, srcPaletteData, palDataSize );

            dstPaletteSize = srcPaletteSize;
        }

        this->paletteData = dstPaletteData;
        this->paletteSize = dstPaletteSize;
    }

    // Clone mipmaps.
    {
        size_t mipmapCount = right.mipmaps.size();

        this->mipmaps.resize( mipmapCount );

        for ( size_t n = 0; n < mipmapCount; n++ )
        {
            const mipmapResource& srcLayer = right.mipmaps[ n ];

            mipmapResource newLayer;

            newLayer.width = srcLayer.width;
            newLayer.height = srcLayer.height;

            newLayer.layerWidth = srcLayer.layerWidth;
            newLayer.layerHeight = srcLayer.layerHeight;

            // Copy the mipmap layer texels.
            uint32 mipDataSize = srcLayer.dataSize;

            const void *srcTexels = srcLayer.texels;

            void *newtexels = engineInterface->PixelAllocate( mipDataSize );

            if ( !newtexels )
            {
                throw RwException( "failed to allocate texel destination buffer in pixel data cloning routine" );
            }

            memcpy( newtexels, srcTexels, mipDataSize );

            newLayer.texels = newtexels;
            newLayer.dataSize = mipDataSize;

            // Store this layer.
            this->mipmaps[ n ] = newLayer;
        }
    }

    // Clone non-trivial parameters.
    this->compressionType = right.compressionType;
    this->hasAlpha = right.hasAlpha;
    this->autoMipmaps = right.autoMipmaps;
    this->cubeTexture = right.cubeTexture;
    this->rasterType = right.rasterType;
}

void pixelDataTraversal::mipmapResource::Free( Interface *engineInterface )
{
    // Free the data here, since we have the Interface struct defined.
    if ( void *ourTexels = this->texels )
    {
        engineInterface->PixelFree( ourTexels );

        // We have no more texels.
        this->texels = NULL;
    }
}

}