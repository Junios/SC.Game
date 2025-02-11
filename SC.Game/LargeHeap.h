#pragma once

namespace SC::Game
{
	class LargeHeap : public IUnknown
	{
		UINT64 mRefCount = 0;
		UINT64 mSizeInBytes;

	public:
		ComPtr<ID3D12Resource> pResource;
		ComPtr<ID3D12Resource> pUploadHeaps[2];

		D3D12_GPU_VIRTUAL_ADDRESS mStartAddress;
		void* pUploadAddress[2];
		std::map<UINT64, UINT64> mWriteRanges[2];
		D3D12_RESOURCE_STATES mInitialState;

	public:
		LargeHeap( D3D12_RESOURCE_STATES initialState, UINT64 sizeInBytes );
		virtual ~LargeHeap();

		HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, _COM_Outptr_ void** ppvObject ) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;

		void* Map();
		void Unmap( const D3D12_RANGE& range );
		void Unmap();
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress();

		bool IsCommittable( int frameIndex );
		void Commit( int frameIndex, CDeviceContext& deviceContext );
	};
}