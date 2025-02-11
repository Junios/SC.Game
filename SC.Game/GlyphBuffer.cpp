using namespace SC;
using namespace SC::Game;

using namespace std;

#pragma unmanaged

GlyphBuffer::GlyphBuffer( const wstring_view& fontFamilyName, float fontSize )
{
	auto pDWriteFactory = Graphics::mFactory->pDWriteFactory.Get();
	auto pDevice = Graphics::mDevice->pDevice.Get();
	auto pDevice11On12 = Graphics::mDevice->pDevice11On12.Get();
	auto pDeviceContext2D = Graphics::mDevice->pDeviceContext2D.Get();

	// 텍스트 렌더링을 위한 텍스트 포맷을 만들고, 텍스트 정보를 조회합니다.
	ComPtr<IDWriteFontCollection> pFontCollection;
	ComPtr<IDWriteFontFamily> pFontFamily;
	HR( pDWriteFactory->CreateTextFormat( fontFamilyName.data(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"ko-KR", &pTextFormat ) );
	HR( pTextFormat->GetFontCollection( &pFontCollection ) );

	UINT index = 0;
	BOOL exist = 0;
	HR( pFontCollection->FindFamilyName( fontFamilyName.data(), &index, &exist ) );
	if ( exist )
	{
		HR( pFontCollection->GetFontFamily( index, &pFontFamily ) );
	}
	else
	{
		HR( pFontCollection->GetFontFamily( 0, &pFontFamily ) );
	}

	// 알파 단색 브러시를 생성합니다.
	HR( pDeviceContext2D->CreateSolidColorBrush( D2D1::ColorF( 1.0f, 1.0f, 1.0f, 1.0f ), &pAlpha1Brush ) );

	// 글리프 최소 정보를 조회합니다.
	HR( pFontFamily->GetFont( 0, &pFont ) );

	DWRITE_FONT_METRICS fontMetrics{ };
	pFont->GetMetrics( &fontMetrics );

	float asdescent = ( float )( fontMetrics.ascent + fontMetrics.descent ) * fontSize / ( float )fontMetrics.designUnitsPerEm;
	maxWidth = 1024;
	maxHeight = asdescent;
	maxHeightAligned = ( UINT )( asdescent + 0.99f );
	designUnitsPerEm = ( float )fontMetrics.designUnitsPerEm;
	fontEmSize = fontSize;
	designSize = fontEmSize / designUnitsPerEm;
	ascent = ( float )( fontMetrics.ascent ) * designSize;
	descent = ( float )( fontMetrics.descent ) * designSize;

	// 글리프 저장을 위한 텍스처 버퍼를 생성합니다.
	D3D12_HEAP_PROPERTIES heapProp{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_DESC textureDesc{ };
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Width = maxWidth;
	textureDesc.Height = maxHeightAligned;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.MipLevels = 1;
	textureDesc.Format = Format;
	textureDesc.SampleDesc = { 1, 0 };
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clearValue{ Format };
	HR( pDevice->CreateCommittedResource( &heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS( &pGlyphTexture ) ) );
	mShaderResourceView = move( DescriptorAllocator::CreateShaderResourceView( pGlyphTexture.Get(), nullptr ) );

	D3D11_RESOURCE_FLAGS resourceFlags{ D3D11_BIND_RENDER_TARGET };
	D2D1_BITMAP_PROPERTIES1 bitmapProp{ };
	bitmapProp.pixelFormat = D2D1::PixelFormat( Format, D2D1_ALPHA_MODE_PREMULTIPLIED );
	bitmapProp.dpiX = 96.0f;
	bitmapProp.dpiY = 96.0f;
	bitmapProp.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_TARGET;
	ComPtr<IDXGISurface> pDxgiSurface;

	HR( pDevice11On12->CreateWrappedResource( pGlyphTexture.Get(), &resourceFlags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, IID_PPV_ARGS( &pGlyphTextureInterop ) ) );
	HR( pGlyphTextureInterop.As<IDXGISurface>( &pDxgiSurface ) );
	HR( pDeviceContext2D->CreateBitmapFromDxgiSurface( pDxgiSurface.Get(), bitmapProp, &pGlyphBitmap ) );

	// 글리프 렌더링 명령을 수행할 장치 컨텍스트 목록을 생성합니다.
	mDeviceContext = CDeviceContext( D3D12_COMMAND_LIST_TYPE_DIRECT );
	mDeviceContext.Reset( App::mFrameIndex );
	mDeviceContext.TransitionBarrier(
		pGlyphTexture.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		0
	);
	mDeviceContext.Close();

	{
		auto lock = UISystem::mMutex.Lock();
		UISystem::mGlyphBuffers.insert( this );
	}
}

GlyphBuffer::~GlyphBuffer()
{
	if ( !UISystem::mDisposed )
	{
		auto lock = UISystem::mMutex.Lock();
		UISystem::mGlyphBuffers.erase( this );
	}

	App::mHeapCommitDCAndGC.GCAdd( move( pGlyphTexture ) );
}

void GlyphBuffer::Restart()
{
	locked.clear();
	added.clear();

	lockedWidth = 0;
	appendWidth = 0;
}

void GlyphBuffer::PushGlyphRun( const DWRITE_GLYPH_RUN* glyphRun )
{
	int glyphCount = glyphRun->glyphCount;

	vector<DWRITE_GLYPH_METRICS> glyphMetrics( glyphCount );
	HR( glyphRun->fontFace->GetDesignGlyphMetrics( glyphRun->glyphIndices, glyphCount, glyphMetrics.data() ) );

	for ( int i = 0; i < glyphCount; ++i )
	{
		auto& glyph = glyphMetrics[i];

		tag_GlyphInfo glyphInfo;
		glyphInfo.glyphIndex = glyphRun->glyphIndices[i];
		glyphInfo.pFontFace = glyphRun->fontFace;
		glyphInfo.offsetX = ( float )glyph.leftSideBearing * designSize - padding;
		glyphInfo.width = ( float )( ( int )glyphMetrics[i].advanceWidth - glyph.leftSideBearing - glyph.rightSideBearing ) * designSize + padding * 2.0f;

		if ( auto it = allocated.find( glyphInfo ); it != allocated.end() )
		{
			auto [iter, flag] = locked.insert( glyphInfo );
			if ( flag ) lockedWidth += glyphInfo.width;
		}
		else
		{
			auto [iter, flag] = added.insert( glyphInfo );
			if ( flag ) appendWidth += glyphInfo.width;
		}
	}
}

void GlyphBuffer::LockGlyphs()
{
	auto lock = glyphLocking.Lock();

	bool forceClear = false;

	// 크기가 부족할 경우 확장합니다.
	if ( lastGlyphOffset + appendWidth > ( float )maxWidth )
	{
		Expand();
		forceClear = true;
	}

	ID3D11On12Device* pDevice11On12 = nullptr;
	list<tag_GlyphInfo> glyphQueue;

	// 추가해야 할 글리프가 있으면 렌더링 준비를 합니다.
	if ( added.size() )
	{
		auto pCommandQueue = Graphics::mUIQueue->pCommandQueue;

		// 자원을 렌더 타겟 상태로 변경합니다.
		Graphics::mUIQueue->Execute( mDeviceContext );

		// 자원을 D2D 상태로 승인합니다.
		pDevice11On12 = Graphics::mDevice->pDevice11On12.Get();
		pDevice11On12->AcquireWrappedResources( pGlyphTextureInterop.GetAddressOf(), 1 );

		glyphQueue.insert( glyphQueue.end(), added.begin(), added.end() );
	}

	// 버퍼를 초과할 경우 버퍼를 초기화합니다.
	if ( forceClear || ( lastGlyphOffset + appendWidth > ( float )maxWidth ) )
	{
		allocated.clear();
		lastGlyphOffset = 0;

		glyphQueue.insert( glyphQueue.end(), locked.begin(), locked.end() );
	}

	if ( pDevice11On12 )
	{
		auto pDeviceContext = Graphics::mDevice->pDeviceContext2D.Get();

		pDeviceContext->SetTarget( pGlyphBitmap.Get() );
		pDeviceContext->BeginDraw();

		if ( allocated.empty() )
		{
			// 글리프를 처음부터 써야 하는 경우 화면을 클리어합니다.
			pDeviceContext->Clear();
		}

		// 각 글리프에 대해 렌더링을 수행합니다.
		while ( UINT maxQueueSize = ( UINT )glyphQueue.size() )
		{
			float appendWidth = 0;
			DWRITE_GLYPH_RUN glyphRun{};
			glyphRun.fontFace = glyphQueue.front().pFontFace;
			glyphRun.fontEmSize = fontEmSize;

			vector<float> glyphAdvances( maxQueueSize );
			vector<DWRITE_GLYPH_OFFSET> glyphOffsets( maxQueueSize );
			vector<UINT16> glyphIndices( maxQueueSize );

			glyphRun.glyphAdvances = glyphAdvances.data();
			glyphRun.glyphOffsets = glyphOffsets.data();
			glyphRun.glyphIndices = glyphIndices.data();

			for ( int i = 0, count = ( int )glyphQueue.size(); i < count; ++i )
			{
				auto glyph = glyphQueue.front();
				glyphQueue.pop_front();

				if ( glyph.pFontFace == glyphRun.fontFace )
				{
					float advance = ( float )( ( int )( glyph.width + 0.9f ) );

					// 글리프 런 정보를 수정합니다.
					glyphAdvances[glyphRun.glyphCount] = advance;
					glyphOffsets[glyphRun.glyphCount].advanceOffset = -glyph.offsetX;
					glyphOffsets[glyphRun.glyphCount].ascenderOffset = descent;
					glyphIndices[glyphRun.glyphCount] = glyph.glyphIndex;
					glyphRun.glyphCount += 1;

					// 할당된 글리프 목록을 갱신합니다.
					allocated.insert( { glyph, lastGlyphOffset + appendWidth } );
					appendWidth += advance;
				}
				else
				{
					glyphQueue.push_back( glyph );
				}
			}

			pDeviceContext->DrawGlyphRun( D2D1::Point2F( lastGlyphOffset, maxHeight ), &glyphRun, pAlpha1Brush.Get() );
			lastGlyphOffset += appendWidth;
		}

		HR( pDeviceContext->EndDraw() );
		pDeviceContext->SetTarget( nullptr );

		// 렌더링 상태를 종료하고 명시적으로 출력합니다.
		pDevice11On12->ReleaseWrappedResources( pGlyphTextureInterop.GetAddressOf(), 1 );
		Graphics::mDevice->pDeviceContext11->Flush();

		// UI 큐에 신호를 보냅니다.
		Graphics::mUIQueue->Signal();
	}
}

void GlyphBuffer::DrawGlyphRun( CDeviceContext* clientDrawingContext, float baselineX, float baselineY, const DWRITE_GLYPH_RUN* glyphRun )
{
	// 각 글리프에 대해 글리프 정보를 작성합니다.
	vector<tag_ShaderInfo> glyphInfo( glyphRun->glyphCount );
	auto bufferPtr = glyphInfo.data();

	static thread_local UINT16 glyphIndices[512];
	static thread_local FLOAT glyphAdvances[512];
	static thread_local DWRITE_GLYPH_OFFSET glyphOffsets[512];
	DWRITE_GLYPH_RUN nextPush{ };

	nextPush.fontEmSize = glyphRun->fontEmSize;
	nextPush.glyphCount = 0;
	nextPush.glyphIndices = glyphIndices;
	nextPush.glyphAdvances = glyphRun->glyphAdvances ? glyphAdvances : nullptr;
	nextPush.glyphOffsets = glyphRun->glyphOffsets ? glyphOffsets : nullptr;
	nextPush.isSideways = glyphRun->isSideways;
	nextPush.bidiLevel = glyphRun->bidiLevel;

	nextPush.fontFace = glyphRun->fontFace;

	float advanceAcc = 0;
	for ( int i = 0; i < ( int )glyphRun->glyphCount; ++i )
	{
		tag_GlyphInfo glyph{ };
		glyph.glyphIndex = glyphRun->glyphIndices[i];
		glyph.pFontFace = glyphRun->fontFace;

		if ( auto it = allocated.find( glyph ); it != allocated.end() )
		{
			auto& glyph = it->first;
			auto& offset = it->second;

			auto& input = bufferPtr[i].input;
			auto& output = bufferPtr[i].output;

			input.left = offset;
			input.top = 0.0f;
			input.width = glyph.width;
			input.height = maxHeight;

			output.left = baselineX + advanceAcc + glyph.offsetX;
			output.top = baselineY - ascent;
			output.width = glyph.width;
			output.height = ascent + descent;
		}
		else
		{
			bufferPtr[i] = { };

			glyphIndices[nextPush.glyphCount] = glyphRun->glyphIndices[i];
			if ( glyphRun->glyphAdvances ) glyphAdvances[nextPush.glyphCount] = glyphRun->glyphAdvances[i];
			if ( glyphRun->glyphOffsets ) glyphOffsets[nextPush.glyphCount] = glyphRun->glyphOffsets[i];

			nextPush.glyphCount += 1;
		}
		advanceAcc += glyphRun->glyphAdvances[i];
	}

	// 글리프 렌더링 목록에 현재 글리프 목록을 추가합니다. 다음 프레임에서 구현됩니다.
	PushGlyphRun( &nextPush );

	clientDrawingContext->SetGraphicsRootShaderResource( Slot_UI_Image, mShaderResourceView );

	UINT64 bufferBegin = sizeof( tag_ShaderInfo ) * UISystem::mShaderDispatchInfoIndex;
	UINT64 bufferRange = sizeof( tag_ShaderInfo ) * glyphRun->glyphCount;

	D3D12_RANGE range;
	range.Begin = bufferBegin;
	range.End = bufferBegin + bufferRange;

	auto block = ( char* )UISystem::mShaderDispatchInfo->Map();
	memcpy( block + bufferBegin, bufferPtr, bufferRange );
	UISystem::mShaderDispatchInfo->Unmap( range );

	auto gpuAddr = UISystem::mShaderDispatchInfo->GetGPUVirtualAddress();
	gpuAddr += bufferBegin;

	clientDrawingContext->SetGraphicsRootShaderResourceView( Slot_UI_ShaderInfo, gpuAddr );
	clientDrawingContext->DrawInstanced( 4, glyphRun->glyphCount );

	// 셰이더 정보 개체의 위치를 넘깁니다.
	UISystem::mShaderDispatchInfoIndex += glyphRun->glyphCount;
}

void GlyphBuffer::Expand()
{
	auto pDWriteFactory = Graphics::mFactory->pDWriteFactory.Get();
	auto pDevice = Graphics::mDevice->pDevice.Get();
	auto pDevice11On12 = Graphics::mDevice->pDevice11On12.Get();
	auto pDeviceContext2D = Graphics::mDevice->pDeviceContext2D.Get();

	// 크기를 2배로 확장합니다.
	maxWidth *= 2;

	// 글리프 저장을 위한 텍스처 버퍼를 생성합니다.
	D3D12_HEAP_PROPERTIES heapProp{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_DESC textureDesc{ };
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Width = maxWidth;
	textureDesc.Height = maxHeightAligned;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.MipLevels = 1;
	textureDesc.Format = Format;
	textureDesc.SampleDesc = { 1, 0 };
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clearValue{ Format };

	// 완성되지 않은 프레임을 위해 이전 텍스처를 남겨둡니다.
	App::mHeapCommitDCAndGC.GCAdd( move( pGlyphTexture ) );

	HR( pDevice->CreateCommittedResource( &heapProp, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS( &pGlyphTexture ) ) );
	mShaderResourceView = DescriptorAllocator::CreateShaderResourceView( pGlyphTexture.Get(), nullptr );

	D3D11_RESOURCE_FLAGS resourceFlags{ D3D11_BIND_RENDER_TARGET };
	D2D1_BITMAP_PROPERTIES1 bitmapProp{ };
	bitmapProp.pixelFormat = D2D1::PixelFormat( Format, D2D1_ALPHA_MODE_PREMULTIPLIED );
	bitmapProp.dpiX = 96.0f;
	bitmapProp.dpiY = 96.0f;
	bitmapProp.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_TARGET;
	ComPtr<IDXGISurface> pDxgiSurface;

	HR( pDevice11On12->CreateWrappedResource( pGlyphTexture.Get(), &resourceFlags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, IID_PPV_ARGS( &pGlyphTextureInterop ) ) );
	HR( pGlyphTextureInterop.As<IDXGISurface>( &pDxgiSurface ) );
	HR( pDeviceContext2D->CreateBitmapFromDxgiSurface( pDxgiSurface.Get(), bitmapProp, &pGlyphBitmap ) );

	// 글리프 렌더링 명령을 수행할 장치 컨텍스트 목록을 생성합니다.
	mDeviceContext.Reset( App::mFrameIndex );
	mDeviceContext.TransitionBarrier(
		pGlyphTexture.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		0
	);
	mDeviceContext.Close();
}