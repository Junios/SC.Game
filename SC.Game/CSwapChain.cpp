using namespace SC;
using namespace SC::Game;

using namespace std;

#pragma unmanaged

CSwapChain::CSwapChain()
{
	auto pDXGIFactory = Graphics::mFactory->pDXGIFactory.Get();
	auto pDevice = Graphics::mDevice->pDevice.Get();
	auto pCommandQueue = Graphics::mCoreQueue->pCommandQueue.Get();

	// 전역 정보를 기반으로 스왑 체인 개체를 생성합니다.
	ComPtr<IDXGISwapChain1> pSwapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{ };
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc = { 1, 0 };
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = ( UINT )BufferCount;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	HR( pDXGIFactory->CreateSwapChainForHwnd(
		pCommandQueue,
		App::hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&pSwapChain
	) );

	HR( pDXGIFactory->MakeWindowAssociation( App::hWnd, DXGI_MWA_NO_WINDOW_CHANGES ) );
	HR( pSwapChain.As<IDXGISwapChain4>( &this->pSwapChain ) );

	// RTV 서술자 힙을 생성합니다.
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{ };
	heapDesc.NumDescriptors = BufferCount;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	HR( pDevice->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &mRTV ) ) );
	mRTVStride = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
}

void CSwapChain::ResizeBuffers( int width, int height )
{
	auto pDevice = Graphics::mDevice->pDevice.Get();

	// 버퍼의 크기를 변경하기 전 모든 참조를 제거합니다.
	for ( int i = 0; i < BufferCount; ++i )
	{
		ppBackBuffers[i] = nullptr;
	}

	// 버퍼의 크기를 변경합니다.
	HR( pSwapChain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, 0 ) );

	// 새로운 버퍼 참조와 함께 렌더 타겟 서술자를 생성합니다.
	auto handleBase = mRTV->GetCPUDescriptorHandleForHeapStart();
	for ( int i = 0; i < BufferCount; ++i )
	{
		HR( pSwapChain->GetBuffer( i, IID_PPV_ARGS( &ppBackBuffers[i] ) ) );
		pDevice->CreateRenderTargetView( ppBackBuffers[i].Get(), nullptr, handleBase );
		RTVHandle[i] = handleBase;
		handleBase.ptr += mRTVStride;

#if defined( _DEBUG )
		wstringstream wss;
		wss << L"SC.Game.CSwapChain.ppBackBuffers[" << i << L"]";
		ppBackBuffers[i]->SetName( wss.str().c_str() );
#endif
	}
}

void CSwapChain::Present( bool vSync )
{
	HR( pSwapChain->Present( ( UINT )vSync, 0 ) );
}

int CSwapChain::Index()
{
	return pSwapChain->GetCurrentBackBufferIndex();
}