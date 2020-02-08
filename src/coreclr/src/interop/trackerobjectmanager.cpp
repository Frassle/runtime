// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// Runtime headers
#include <volatile.h>

#include "comwrappers.h"
#include <interoplibimports.h>

using OBJECTHANDLE = InteropLib::OBJECTHANDLE;
using RuntimeCallContext = InteropLibImports::RuntimeCallContext;

namespace
{
    const IID IID_IReferenceTrackerHost = __uuidof(IReferenceTrackerHost);
    const IID IID_IReferenceTrackerTarget = __uuidof(IReferenceTrackerTarget);
    const IID IID_IReferenceTracker = __uuidof(IReferenceTracker);
    const IID IID_IReferenceTrackerManager = __uuidof(IReferenceTrackerManager);
    const IID IID_IFindReferenceTargetsCallback = __uuidof(IFindReferenceTargetsCallback);

    // In order to minimize the impact of a constructor running on module load,
    // the HostServices class should have no instance fields.
    class HostServices : public IReferenceTrackerHost
    {
    public: // static
        static Volatile<OBJECTHANDLE> RuntimeImpl;

    public: // IReferenceTrackerHost
        STDMETHOD(DisconnectUnusedReferenceSources)(_In_ DWORD dwFlags);
        STDMETHOD(ReleaseDisconnectedReferenceSources)();
        STDMETHOD(NotifyEndOfReferenceTrackingOnThread)();
        STDMETHOD(GetTrackerTarget)(_In_ IUnknown* obj, _Outptr_ IReferenceTrackerTarget** ppNewReference);
        STDMETHOD(AddMemoryPressure)(_In_ UINT64 bytesAllocated);
        STDMETHOD(RemoveMemoryPressure)(_In_ UINT64 bytesAllocated);

    public: // IUnknown
        // Lifetime maintained by stack - we don't care about ref counts
        STDMETHOD_(ULONG, AddRef)() { return 1; }
        STDMETHOD_(ULONG, Release)() { return 1; }

        STDMETHOD(QueryInterface)(
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject)
        {
            if (ppvObject == nullptr)
                return E_POINTER;

            if (IsEqualIID(riid, IID_IReferenceTrackerHost))
            {
                *ppvObject = static_cast<IReferenceTrackerHost*>(this);
            }
            else if (IsEqualIID(riid, IID_IUnknown))
            {
                *ppvObject = static_cast<IUnknown*>(this);
            }
            else
            {
                *ppvObject = nullptr;
                return E_NOINTERFACE;
            }

            (void)AddRef();
            return S_OK;
        }
    };

    // Runtime implementation for some host services.
    Volatile<OBJECTHANDLE> HostServices::RuntimeImpl;

    // Global instance of host services.
    HostServices g_HostServicesInstance;

    // Defined in windows.ui.xaml.hosting.referencetracker.h.
    enum XAML_REFERENCETRACKER_DISCONNECT
    {
        // Indicates the disconnect is during a suspend and a GC can be trigger.
        XAML_REFERENCETRACKER_DISCONNECT_SUSPEND = 0x00000001
    };

    STDMETHODIMP HostServices::DisconnectUnusedReferenceSources(_In_ DWORD flags)
    {
        InteropLibImports::GcRequest type = InteropLibImports::GcRequest::Default;

        // Request an expensive blocking GC when a suspend is occurring.
        if (flags & XAML_REFERENCETRACKER_DISCONNECT_SUSPEND)
            type = InteropLibImports::GcRequest::Blocking;

        return InteropLibImports::RequestGarbageCollectionForExternal(type);
    }

    STDMETHODIMP HostServices::ReleaseDisconnectedReferenceSources()
    {
        return InteropLibImports::WaitForRuntimeFinalizerForExternal();
    }

    STDMETHODIMP HostServices::NotifyEndOfReferenceTrackingOnThread()
    {
        OBJECTHANDLE impl = HostServices::RuntimeImpl;
        if (impl == nullptr)
            return E_NOT_SET;

        return InteropLibImports::ReleaseExternalObjectsFromCurrentThread(impl);
    }

    //
    // Creates a proxy object that points to the given RCW
    // The proxy
    // 1. Has a managed reference pointing to the RCW, and therefore forms a cycle that can be resolved by GC
    // 2. Forwards data binding requests
    // For example:
    //
    // Grid <---- RCW             Grid <-------- RCW
    // | ^                         |              ^
    // | |             Becomes     |              |
    // v |                         v              |
    // Rectangle                  Rectangle ----->Proxy
    //
    // Arguments
    //   obj        - The identity IUnknown* where a RCW points to (Grid, in this case)
    //                    Note that
    //                    1) we can either create a new RCW or get back an old one from cache
    //                    2) This obj could be a regular WinRT object (such as WinRT collection) for data binding
    //  ppNewReference  - The IReferenceTrackerTarget* for the proxy created
    //                    Jupiter will call IReferenceTrackerTarget to establish a jupiter reference
    //
    STDMETHODIMP HostServices::GetTrackerTarget(_In_ IUnknown* obj, _Outptr_ IReferenceTrackerTarget** ppNewReference)
    {
        if (obj == nullptr || ppNewReference == nullptr)
            return E_INVALIDARG;

        HRESULT hr;

        OBJECTHANDLE impl = HostServices::RuntimeImpl;
        if (impl == nullptr)
            return E_NOT_SET;

        // QI for IUnknown to get the identity unknown
        ComHolder<IUnknown> identity;
        RETURN_IF_FAILED(obj->QueryInterface(&identity));

        // Get or create an existing implementation for this external.
        ComHolder<IUnknown> target;
        RETURN_IF_FAILED(InteropLibImports::GetOrCreateTrackerTargetForExternal(
            impl,
            identity,
            (INT32)CreateObjectFlags::TrackerObject,
            (INT32)CreateComInterfaceFlags::TrackerSupport,
            (void**)&target));

        return target->QueryInterface(IID_IReferenceTrackerTarget, (void**)ppNewReference);
    }

    STDMETHODIMP HostServices::AddMemoryPressure(_In_ UINT64 bytesAllocated)
    {
        return InteropLibImports::AddMemoryPressureForExternal(bytesAllocated);
    }

    STDMETHODIMP HostServices::RemoveMemoryPressure(_In_ UINT64 bytesAllocated)
    {
        return InteropLibImports::RemoveMemoryPressureForExternal(bytesAllocated);
    }

    VolatilePtr<IReferenceTrackerManager> s_TrackerManager; // The one and only Tracker Manager instance
    Volatile<BOOL> s_HasTrackingStarted = FALSE;

    // Indicates if walking the external objects is needed.
    // (i.e. Have any IReferenceTracker instances been found?)
    bool ShouldWalkExternalObjects()
    {
        return (s_TrackerManager != nullptr);
    }

    // Callback implementation of IFindReferenceTargetsCallback
    class FindDependentWrappersCallback : public IFindReferenceTargetsCallback
    {
        NativeObjectWrapperContext* _nowCxt;
        RuntimeCallContext* _runtimeCallCxt;

    public:
        FindDependentWrappersCallback(_In_ NativeObjectWrapperContext* nowCxt, _In_ RuntimeCallContext* runtimeCallCxt)
            : _nowCxt{ nowCxt }
            , _runtimeCallCxt{ runtimeCallCxt }
        {
            _ASSERTE(_nowCxt != nullptr && runtimeCallCxt != nullptr);
        }

        STDMETHOD(FoundTrackerTarget)(_In_ IReferenceTrackerTarget* target)
        {
            HRESULT hr;

            if (target != nullptr)
                return E_POINTER;

            ManagedObjectWrapper* mow = ManagedObjectWrapper::MapFromIUnknown(target);

            // Not a target we implemented.
            if (mow == nullptr)
                return S_OK;

            // Notify the runtime a reference path was found.
            RETURN_IF_FAILED(InteropLibImports::FoundReferencePath(
                _runtimeCallCxt,
                _nowCxt->GetRuntimeContext(),
                mow->Target));

            return S_OK;
        }

        // Lifetime maintained by stack - we don't care about ref counts
        STDMETHOD_(ULONG, AddRef)() { return 1; }
        STDMETHOD_(ULONG, Release)() { return 1; }

        STDMETHOD(QueryInterface)(
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject)
        {
            if (ppvObject == nullptr)
                return E_POINTER;

            if (IsEqualIID(riid, IID_IFindReferenceTargetsCallback))
            {
                *ppvObject = static_cast<IFindReferenceTargetsCallback*>(this);
            }
            else if (IsEqualIID(riid, IID_IUnknown))
            {
                *ppvObject = static_cast<IUnknown*>(this);
            }
            else
            {
                *ppvObject = nullptr;
                return E_NOINTERFACE;
            }

            (void)AddRef();
            return S_OK;
        }
    };

    HRESULT WalkExternalTrackerObjects(_In_ RuntimeCallContext* cxt)
    {
        _ASSERTE(cxt != nullptr);

        BOOL walkFailed = FALSE;
        HRESULT hr;

        void* extObjContext = nullptr;
        while (S_OK == (hr = InteropLibImports::IteratorNext(cxt, &extObjContext)))
        {
            _ASSERTE(extObjContext != nullptr);

            NativeObjectWrapperContext* nowc = NativeObjectWrapperContext::MapFromRuntimeContext(extObjContext);

            // Check if the object is a tracker object.
            IReferenceTracker* trackerMaybe = nowc->GetReferenceTracker();
            if (trackerMaybe == nullptr)
                continue;

            // Ask the tracker instance to find all reference targets.
            FindDependentWrappersCallback cb{ nowc, cxt };
            hr = trackerMaybe->FindTrackerTargets(&cb);
            if (FAILED(hr))
                break;
        }

        if (FAILED(hr))
        {
            // Remember the fact that we've failed and stop walking
            walkFailed = TRUE;
            InteropLibImports::SetGlobalPeggingState(true);
        }

        _ASSERTE(s_TrackerManager != nullptr);
        (void)s_TrackerManager->FindTrackerTargetsCompleted(walkFailed);

        return hr;
    }
}

bool TrackerObjectManager::TrySetReferenceTrackerHostRuntimeImpl(_In_ OBJECTHANDLE objectHandle, _In_ OBJECTHANDLE current)
{
    // Attempt to set the runtime implementation for providing hosting services to the tracker runtime. 
    return (::InterlockedCompareExchangePointer(
        &HostServices::RuntimeImpl,
        objectHandle, current) == current);
}

HRESULT TrackerObjectManager::OnIReferenceTrackerFound(_In_ IReferenceTracker* obj)
{
    _ASSERTE(obj != nullptr);
    if (s_TrackerManager != nullptr)
        return S_OK;

    // Retrieve IReferenceTrackerManager
    HRESULT hr;
    ComHolder<IReferenceTrackerManager> trackerManager;
    RETURN_IF_FAILED(obj->GetReferenceTrackerManager(&trackerManager));

    ComHolder<IReferenceTrackerHost> hostServices;
    RETURN_IF_FAILED(g_HostServicesInstance.QueryInterface(IID_IReferenceTrackerHost, (void**)&hostServices));

    // Attempt to set the tracker instance.
    if (::InterlockedCompareExchangePointer((void**)&s_TrackerManager, trackerManager.p, nullptr) == nullptr)
    {
        (void)trackerManager.Detach(); // Ownership has been transfered
        RETURN_IF_FAILED(s_TrackerManager->SetReferenceTrackerHost(hostServices));
    }

    return S_OK;
}

HRESULT TrackerObjectManager::AfterWrapperCreated(_In_ IReferenceTracker* obj)
{
    _ASSERTE(obj != nullptr);

    HRESULT hr;

    // Notify tracker runtime that we've created a new wrapper for this object.
    // To avoid surprises, we should notify them before we fire the first AddRefFromTrackerSource.
    RETURN_IF_FAILED(obj->ConnectFromTrackerSource());

    // Send out AddRefFromTrackerSource callbacks to notify tracker runtime we've done AddRef()
    // for certain interfaces. We should do this *after* we made a AddRef() because we should never
    // be in a state where report refs > actual refs
    RETURN_IF_FAILED(obj->AddRefFromTrackerSource());

    return S_OK;
}

HRESULT TrackerObjectManager::BeforeWrapperDestroyed(_In_ IReferenceTracker* obj)
{
    _ASSERTE(obj != nullptr);

    HRESULT hr;

    // Notify tracker runtime that we are about to destroy a wrapper
    // (same timing as short weak handle) for this object.
    // They need this information to disconnect weak refs and stop firing events,
    // so that they can avoid resurrecting the object.
    RETURN_IF_FAILED(obj->DisconnectFromTrackerSource());

    return S_OK;
}

HRESULT TrackerObjectManager::BeginReferenceTracking(_In_ RuntimeCallContext* cxt)
{
    _ASSERTE(cxt != nullptr);

    if (!ShouldWalkExternalObjects())
        return S_FALSE;

    HRESULT hr;

    _ASSERTE(s_HasTrackingStarted == FALSE);
    _ASSERTE(InteropLibImports::GetGlobalPeggingState());

    s_HasTrackingStarted = TRUE;

    // From this point, the tracker runtime decides whether a target
    // should be pegged or not as the global pegging flag is now off.
    InteropLibImports::SetGlobalPeggingState(false);

    // Let the tracker runtime know we are about to walk external objects so that
    // they can lock their reference cache. Note that the tracker runtime doesn't need to
    // unpeg all external objects at this point and they can do the pegging/unpegging.
    // in FindTrackerTargetsCompleted.
    _ASSERTE(s_TrackerManager != nullptr);
    RETURN_IF_FAILED(s_TrackerManager->ReferenceTrackingStarted());

    // Time to walk the external objects
    RETURN_IF_FAILED(WalkExternalTrackerObjects(cxt));

    return S_OK;
}

HRESULT TrackerObjectManager::EndReferenceTracking()
{
    if (s_HasTrackingStarted != TRUE
        || !ShouldWalkExternalObjects())
        return S_FALSE;

    HRESULT hr;

    // Let the tracker runtime know the external object walk is done and they need to:
    // 1. Unpeg all managed object wrappers (mow) if the (mow) needs to be unpegged
    //       (i.e. when the (mow) is only reachable by other external tracker objects).
    // 2. Peg all mows if the mow needs to be pegged (i.e. when the above condition is not true)
    // 3. Unlock reference cache when they are done.
    _ASSERTE(s_TrackerManager != nullptr);
    hr = s_TrackerManager->ReferenceTrackingCompleted();
    _ASSERTE(SUCCEEDED(hr));

    InteropLibImports::SetGlobalPeggingState(true);
    s_HasTrackingStarted = FALSE;

    return hr;
}

void TrackerObjectManager::OnShutdown()
{
    IReferenceTrackerManager* trackerManager = s_TrackerManager;
    if (::InterlockedCompareExchangePointer((void**)&s_TrackerManager, nullptr, trackerManager) == trackerManager)
    {
        // Make sure s_TrackerManager is either null or a valid pointer.
        trackerManager->Release();
    }
}
