/*	see copyright notice in squirrel.h */
#ifndef _SQUSERDATA_H_
#define _SQUSERDATA_H_

struct SQUserData : SQDelegable
{
	SQUserData(SQSharedState *ss, SQInteger size){ _delegate = nullptr; _hook = nullptr; INIT_CHAIN(); ADD_TO_CHAIN(&_ss(this)->_gc_chain, this); _size = size; _typetag = nullptr;
}
	~SQUserData()
	{
		REMOVE_FROM_CHAIN(&_ss(this)->_gc_chain, this);
		SetDelegate(nullptr);
	}
	static SQUserData* Create(SQSharedState *ss, SQInteger size)
	{
		SQUserData *ud = new (SQSizedAllocationTag(sizeof(SQUserData)+(size-1))) SQUserData(ss, size);
		return ud;
	}
#ifndef NO_GARBAGE_COLLECTOR
	void EnqueueMarkObjectForChildren(SQGCMarkerQueue &queue);
	void Finalize(){SetDelegate(nullptr);}
#endif
	void Release() {
		if (_hook) _hook(_val,_size);
		sq_delete_refcounted(this, SQUserData);
	}

	SQInteger _size;
	SQRELEASEHOOK _hook;
	SQUserPointer _typetag;
	SQChar _val[1];
};

#endif //_SQUSERDATA_H_
