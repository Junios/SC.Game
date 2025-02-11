#pragma once

namespace SC::Game::UI
{
	ref class Brush;

	class GlyphRenderer : public IDWriteTextRenderer
	{
		ULONG64 mRefCount = 0;

	public:
		gcroot<Brush^> mFillBrush;
		GlyphBuffer* mGlyphBuffer = nullptr;
		float mOpacity = 1.0f;

	public:
		GlyphRenderer( GlyphBuffer* glyphBuffer );

		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppvObject ) override;

		HRESULT STDMETHODCALLTYPE DrawGlyphRun( void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_MEASURING_MODE measuringMode, DWRITE_GLYPH_RUN const* glyphRun, DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription, IUnknown* clientDrawingEffect ) override;
		HRESULT STDMETHODCALLTYPE DrawUnderline( void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_UNDERLINE const* underline, IUnknown* clientDrawingEffect ) override;
		HRESULT STDMETHODCALLTYPE DrawStrikethrough( void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_STRIKETHROUGH const* strikethrough, IUnknown* clientDrawingEffect ) override;
		HRESULT STDMETHODCALLTYPE DrawInlineObject( void* clientDrawingContext, FLOAT originX, FLOAT originY, IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft, IUnknown* clientDrawingEffect ) override;

		HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled( void* clientDrawingContext, BOOL* isDisabled ) override;
		HRESULT STDMETHODCALLTYPE GetCurrentTransform( void* clientDrawingContext, DWRITE_MATRIX* transform ) override;
		HRESULT STDMETHODCALLTYPE GetPixelsPerDip( void* clientDrawingContext, FLOAT* pixelsPerDip ) override;
	};
}