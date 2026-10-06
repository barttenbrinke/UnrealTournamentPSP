/*=============================================================================
	FMallocPspCheck.h: heap-overrun hunting allocator for the PSP port.

	Every block carries a header (magic, size, tag) and a trailing canary, and
	all live blocks are kept in a list. Canaries are verified when a block is
	freed or reallocated and, for all blocks, every CheckEvery allocations, so
	the first overrun is reported with the tag of the block that was written
	past instead of surfacing much later as a crash inside newlib's free().
	Debug builds only: cmake -DPSP_HEAPCHECK=ON.
=============================================================================*/

class FMallocPspCheck : public FMalloc
{
	enum { HEAD_MAGIC = 0x48454144, TAIL_MAGIC = 0x5441494c, TAIL_BYTES = 16, CheckEvery = 256 };
	struct FHead
	{
		FHead*	Prev;
		FHead*	Next;
		DWORD	Size;
		DWORD	Magic;
		const TCHAR* Tag;
		DWORD	Serial;
		DWORD	Pad[2];		// header stays 32 bytes: user data 16-aligned
	};
	FHead* List;
	DWORD Count, Serial;
	UBOOL Reported;

	static BYTE* Tail( FHead* H ) { return (BYTE*)(H+1) + H->Size; }
	UBOOL Valid( FHead* H, const TCHAR* Where )
	{
		if( H->Magic != HEAD_MAGIC )
			return Report( H, TEXT("header"), Where );
		const BYTE* T = Tail( H );
		for( INT i=0; i<TAIL_BYTES; i+=4 )
		{
			DWORD W; appMemcpy( &W, T+i, 4 );
			if( W != TAIL_MAGIC )
				return Report( H, TEXT("tail"), Where );
		}
		return 1;
	}
	UBOOL Report( FHead* H, const TCHAR* What, const TCHAR* Where )
	{
		if( !Reported )
		{
			Reported = 1;
			debugf( NAME_Warning, TEXT("PSPHEAP: %s of block %p (size %u, tag '%s', alloc #%u) overwritten; noticed in %s"),
				What, H+1, H->Magic==HEAD_MAGIC ? H->Size : 0, H->Magic==HEAD_MAGIC && H->Tag ? H->Tag : TEXT("?"), H->Serial, Where );
			const BYTE* T = Tail( H );
			if( H->Magic == HEAD_MAGIC )
				debugf( NAME_Warning, TEXT("PSPHEAP: tail bytes %02x %02x %02x %02x %02x %02x %02x %02x"), T[0],T[1],T[2],T[3],T[4],T[5],T[6],T[7] );
			Reported = 0;
			*(volatile INT*)0 = 0;	// fault: PPSSPP / PSPLink then show the caller's stack
		}
		return 0;
	}
	void CheckAll( const TCHAR* Where )
	{
		for( FHead* H=List; H; H=H->Next )
			if( !Valid( H, Where ) )
			{
				// Report once per bad block: unlink it so the scan moves on.
				if( H->Prev ) H->Prev->Next = H->Next; else List = H->Next;
				if( H->Next ) H->Next->Prev = H->Prev;
				break;
			}
	}
	void* Wrap( FHead* H, DWORD Size, const TCHAR* Tag )
	{
		H->Size = Size; H->Magic = HEAD_MAGIC; H->Tag = Tag; H->Serial = ++Serial;
		BYTE* T = Tail( H );
		for( INT i=0; i<TAIL_BYTES; i+=4 ) { DWORD W=TAIL_MAGIC; appMemcpy( T+i, &W, 4 ); }
		H->Prev = NULL; H->Next = List; if( List ) List->Prev = H; List = H;
		if( ++Count % CheckEvery == 0 )
			CheckAll( Tag ? Tag : TEXT("alloc") );
		return H+1;
	}
	void Unlink( FHead* H )
	{
		if( H->Prev ) H->Prev->Next = H->Next; else if( List == H ) List = H->Next;
		if( H->Next ) H->Next->Prev = H->Prev;
	}
public:
	FMallocPspCheck() : List(NULL), Count(0), Serial(0), Reported(0) {}
	void* Malloc( DWORD Size, const TCHAR* Tag )
	{
		FHead* H = (FHead*)malloc( sizeof(FHead) + Size + TAIL_BYTES );
		check(H);
		return Wrap( H, Size, Tag );
	}
	void* Realloc( void* Ptr, DWORD NewSize, const TCHAR* Tag )
	{
		if( !Ptr )
			return NewSize ? Malloc( NewSize, Tag ) : NULL;
		FHead* H = (FHead*)Ptr - 1;
		Valid( H, TEXT("realloc") );
		Unlink( H );
		if( !NewSize )
		{
			free( H );
			return NULL;
		}
		H = (FHead*)realloc( H, sizeof(FHead) + NewSize + TAIL_BYTES );
		check(H);
		return Wrap( H, NewSize, Tag );
	}
	void Free( void* Ptr )
	{
		if( !Ptr )
			return;
		FHead* H = (FHead*)Ptr - 1;
		Valid( H, TEXT("free") );
		Unlink( H );
		H->Magic = 0;
		free( H );
	}
	void DumpAllocs() {}
	void HeapCheck() { CheckAll( TEXT("HeapCheck") ); }
	void Init() {}
	void Exit() {}
};
