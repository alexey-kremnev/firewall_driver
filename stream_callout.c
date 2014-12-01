


#include "ntddk.h"

#pragma warning(push)
#pragma warning(disable:4201)       // unnamed struct/union

#include "fwpsk.h"

#pragma warning(pop)

#include "fwpmk.h"


#include "chaeck_permission.h"
#include "stream_callout.h"

#define INITGUID
#include <guiddef.h>

#define DOS_DEVICE_NAME     L"\\DosDevices\\StreamFirewall"
KSPIN_LOCK editLock;

PUCHAR newBuffer = NULL;
ULONG newBufferSize = 1024;
char    pointer[20][64];

// e6011cdc-440b-4a6f-8499-6fdb55fb1f92
DEFINE_GUID(
    STREAM_EDITOR_STREAM_CALLOUT_V4,
    0xe6011cdc,
    0x440b,
    0x4a6f,
    0x84, 0x99, 0x6f, 0xdb, 0x55, 0xfb, 0x1f, 0x92
);
// c0bc07b4-aaf6-4242-a3dc-3ef341ffde5d
DEFINE_GUID(
    STREAM_EDITOR_STREAM_CALLOUT_V6,
    0xc0bc07b4,
    0xaaf6,
    0x4242,
    0xa3, 0xdc, 0x3e, 0xf3, 0x41, 0xff, 0xde, 0x5d
);

DEFINE_GUID(
    TL_INSPECT_SUBLAYER,
    0x2e207682,
    0xd95f,
    0x4525,
    0xb9, 0x66, 0x96, 0x9f, 0x26, 0x58, 0x7f, 0x03
);
// 
// Callout driver global variables
//

//PMDL gStringToReplaceMdl;

STREAM_EDITOR gStreamEditor;
HANDLE  hFile;
HANDLE gEngineHandle;
UINT32 gCalloutIdV4;
UINT32 gCalloutIdV6;

PDEVICE_OBJECT gDeviceObject;

HANDLE gInjectionHandle;

PNDIS_GENERIC_OBJECT gNdisGenericObj;
NDIS_HANDLE gNetBufferListPool;

#define STREAM_EDITOR_NDIS_OBJ_TAG 'oneS'
#define STREAM_EDITOR_NBL_POOL_TAG 'pneS'
#define STREAM_EDITOR_FLAT_BUFFER_TAG 'bfeS'


DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry(
   IN  PDRIVER_OBJECT  driverObject,
   IN  PUNICODE_STRING registryPath
   );

DRIVER_UNLOAD DriverUnload;
VOID
DriverUnload(
   IN  PDRIVER_OBJECT driverObject
   );

NTSTATUS DeviceControlFunction(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

NTSTATUS CreateCloseFunction(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

NTSTATUS
StreamEditNotify(
   IN FWPS_CALLOUT_NOTIFY_TYPE notifyType,
   IN const GUID* filterKey,
   IN const FWPS_FILTER* filter
   )
{
   UNREFERENCED_PARAMETER(notifyType);
   UNREFERENCED_PARAMETER(filterKey);
   UNREFERENCED_PARAMETER(filter);

   return STATUS_SUCCESS;
}

NTSTATUS
RegisterCalloutForLayer(
   IN const GUID* layerKey,
   IN const GUID* calloutKey,
   IN void* deviceObject,
   OUT UINT32* calloutId
   )

{
   NTSTATUS status = STATUS_SUCCESS;
   UINT64 s=0;

   FWPS_CALLOUT sCallout = {0};

   FWPM_FILTER filter = {0};
   FWPM_FILTER_CONDITION filterConditions[2] = {0}; 

   FWPM_CALLOUT mCallout = {0};
   FWPM_DISPLAY_DATA displayData = {0};

   BOOLEAN calloutRegistered = FALSE;

   sCallout.calloutKey = *calloutKey;
		// sCallout.flags          = FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW;
   sCallout.classifyFn = StreamInlineEditClassify;//StreamOobEditClassify;//(configEditInline ? StreamInlineEditClassify :
                                            // StreamOobEditClassify);
   sCallout.notifyFn = StreamEditNotify;

   status = FwpsCalloutRegister(
               deviceObject,
               &sCallout,
               calloutId
               );
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }
   calloutRegistered = TRUE;

   displayData.name = L"Stream Edit Callout";
   displayData.description = L"Callout that finds and replaces a token from a TCP stream";

   mCallout.calloutKey = *calloutKey;
   mCallout.displayData = displayData;
   mCallout.applicableLayer = *layerKey;
   status = FwpmCalloutAdd(
               gEngineHandle,
               &mCallout,
               NULL,
               NULL
               );

   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

   filter.layerKey = *layerKey;
   filter.displayData.name = L"Stream Edit Filter";
   filter.displayData.description = L"Filter that finds and replaces a token from a TCP stream";

   filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
   filter.action.calloutKey = *calloutKey;
   filter.flags              = 0;
   filter.numFilterConditions = 0;
   //filter.filterCondition = filterConditions;
   filter.subLayerKey = FWPM_SUBLAYER_UNIVERSAL;//TL_INSPECT_SUBLAYER;
   filter.weight.type = FWP_EMPTY; // auto-weight.
    //filter.weight.uint64 = &s;

  /*filterConditions[0].fieldKey = FWPM_CONDITION_DIRECTION;
   filterConditions[0].matchType = FWP_MATCH_EQUAL;
   filterConditions[0].conditionValue.type = FWP_UINT32;
   filterConditions[0].conditionValue.uint32 = FWP_DIRECTION_INBOUND;*/

   status = FwpmFilterAdd(
               gEngineHandle,
               &filter,
               NULL,
               NULL);
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

Exit:

   if (!NT_SUCCESS(status))
   {
      if (calloutRegistered)
      {
         FwpsCalloutUnregisterById(*calloutId);
      }
   }

   return status;
}

NTSTATUS
StreamEditRegisterCallout(
   IN const STREAM_EDITOR* streamEditor,
   IN void* deviceObject
   )

{
   NTSTATUS status = STATUS_SUCCESS;

   BOOLEAN engineOpened = FALSE;
   BOOLEAN inTransaction = FALSE;

   FWPM_SESSION session = {0};

   UNREFERENCED_PARAMETER(streamEditor);

   session.flags = FWPM_SESSION_FLAG_DYNAMIC;

   status = FwpmEngineOpen(
                NULL,
                RPC_C_AUTHN_WINNT,
                NULL,
                &session,
                &gEngineHandle
                );
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }
   engineOpened = TRUE;

   status = FwpmTransactionBegin(gEngineHandle, 0);
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }
   inTransaction = TRUE;

   status = RegisterCalloutForLayer(
               &FWPM_LAYER_STREAM_V4,
               &STREAM_EDITOR_STREAM_CALLOUT_V4,
               deviceObject,
               &gCalloutIdV4
               );
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

   status = RegisterCalloutForLayer(
               &FWPM_LAYER_STREAM_V6,
               &STREAM_EDITOR_STREAM_CALLOUT_V6,
               deviceObject,
               &gCalloutIdV6
               );
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

   status = FwpmTransactionCommit(gEngineHandle);
   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }
   inTransaction = FALSE;

Exit:

   if (!NT_SUCCESS(status))
   {
      if (inTransaction)
      {
         FwpmTransactionAbort(gEngineHandle);
      }
      if (engineOpened)
      {
         FwpmEngineClose(gEngineHandle);
         gEngineHandle = NULL;
      }
   }

   return status;
}

void
StreamEditUnregisterCallout()
{
   FwpmEngineClose(gEngineHandle);
   gEngineHandle = NULL;

   FwpsCalloutUnregisterById(gCalloutIdV6);
   FwpsCalloutUnregisterById(gCalloutIdV4);
}

VOID
DriverUnload(
   IN  PDRIVER_OBJECT driverObject
   )
{

   UNREFERENCED_PARAMETER(driverObject);
   ZwClose( hFile );

 

   if (gStreamEditor.scratchBuffer != NULL)
   {
      ExFreePoolWithTag(
         gStreamEditor.scratchBuffer,
         STREAM_EDITOR_FLAT_BUFFER_TAG
         );

   }

   StreamEditUnregisterCallout();

   FwpsInjectionHandleDestroy(gInjectionHandle);

   //NdisFreeNetBufferListPool(gNetBufferListPool);
   //NdisFreeGenericObject(gNdisGenericObj);

   //IoFreeMdl(gStringToReplaceMdl);

    if( newBuffer )
		    ExFreePoolWithTag( newBuffer, '1234');

    IoDeleteDevice(gDeviceObject);
}


NTSTATUS
DriverEntry(
   IN  PDRIVER_OBJECT  driverObject,
   IN  PUNICODE_STRING registryPath
   )
{
  
   NTSTATUS status = STATUS_SUCCESS;

   IO_STATUS_BLOCK  iostatus;
   OBJECT_ATTRIBUTES  ObjectAttributes;
   UNICODE_STRING  deviceName, ntWin32NameString; 
   NET_BUFFER_LIST_POOL_PARAMETERS nblPoolParams = {0};
   //DbgBreakPoint();


   RtlInitUnicodeString(
      &deviceName,
      L"\\Device\\StreamFirewall"
      );

   status = IoCreateDevice(
               driverObject, 
               0, 
               &deviceName, 
               FILE_DEVICE_NETWORK, 
               0, 
               FALSE, 
               &gDeviceObject);
   if (!NT_SUCCESS(status))
   {
       goto Exit;
   }

   RtlInitUnicodeString( &ntWin32NameString, DOS_DEVICE_NAME);
   status = IoCreateSymbolicLink( &ntWin32NameString, &deviceName);
   /*gStringToReplaceMdl = IoAllocateMdl(
                           configStringToInsert,
                           strlen(configStringToInsert),
                           FALSE,
                           FALSE,
                           NULL
                           );
   if (gStringToReplaceMdl == NULL)
   {
      status = STATUS_NO_MEMORY;
      goto Exit;
   }*/

   //MmBuildMdlForNonPagedPool(gStringToReplaceMdl);

  

  /* gNdisGenericObj = NdisAllocateGenericObject(
                        driverObject, 
                        STREAM_EDITOR_NDIS_OBJ_TAG, 
                        0);

   if (gNdisGenericObj == NULL)
   {
      status = STATUS_NO_MEMORY;
      goto Exit;
   }*/

 /*  nblPoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
   nblPoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
   nblPoolParams.Header.Size = sizeof(nblPoolParams);

   nblPoolParams.fAllocateNetBuffer = TRUE;
   nblPoolParams.DataSize = 0;

   nblPoolParams.PoolTag = STREAM_EDITOR_NBL_POOL_TAG;

   gNetBufferListPool = NdisAllocateNetBufferListPool(
                           gNdisGenericObj,
                           &nblPoolParams
                           );

   if (gNetBufferListPool == NULL)
   {
      status = STATUS_NO_MEMORY;
      goto Exit;
   }*/

   status = FwpsInjectionHandleCreate(
               AF_UNSPEC,
               FWPS_INJECTION_TYPE_STREAM,
               &gInjectionHandle );

   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

   status = StreamEditRegisterCallout(
               &gStreamEditor,
               gDeviceObject );

   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

   KeInitializeSpinLock( &editLock );  

   if (!NT_SUCCESS(status))
   {
      goto Exit;
   }

    driverObject->MajorFunction[IRP_MJ_CREATE] = CreateCloseFunction;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = CreateCloseFunction;

    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControlFunction;
    driverObject->DriverUnload = DriverUnload;

    newBuffer = (PUCHAR)ExAllocatePoolWithTag(
								   NonPagedPool,
								   newBufferSize,
								   '1234');


Exit:
   
   if (!NT_SUCCESS(status))
   {
      if (gEngineHandle != NULL)
      {
         StreamEditUnregisterCallout();
      }

      if (gInjectionHandle != NULL)
      {
         FwpsInjectionHandleDestroy(gInjectionHandle);
      }
     /* if (gNetBufferListPool != NULL)
      {
         NdisFreeNetBufferListPool(gNetBufferListPool);
      }*/
     /* if (gNdisGenericObj != NULL)
      {
         NdisFreeGenericObject(gNdisGenericObj);
      }
      if (gStringToReplaceMdl != NULL)
      {
         IoFreeMdl(gStringToReplaceMdl);
      }*/
      if (gDeviceObject)
      {
         IoDeleteDevice(gDeviceObject);
      }
	  if( newBuffer )
		    ExFreePoolWithTag( newBuffer, '1234');
   }

   return status;
}

BOOLEAN
StreamCopyDataForInspection(
   IN STREAM_EDITOR* streamEditor,
   IN const FWPS_STREAM_DATA* streamData
   )


{
   SIZE_T bytesCopied;

   SIZE_T existingDataLength = streamEditor->dataLength;
   ASSERT(streamEditor->dataOffset == 0);
   if (streamEditor->bufferSize - existingDataLength < streamData->dataLength)
   {
      SIZE_T newBufferSize = (streamData->dataLength + existingDataLength) * 2;
      PVOID newBuffer = ExAllocatePoolWithTag(
                           NonPagedPool,
                           newBufferSize,
                           STREAM_EDITOR_FLAT_BUFFER_TAG
                           );

      if (newBuffer != NULL)
      {
         if (existingDataLength > 0)
         {
            ASSERT(streamEditor->scratchBuffer != NULL);
            RtlCopyMemory(
               newBuffer, 
               streamEditor->scratchBuffer, 
               existingDataLength
               );
         }
      }

      if (streamEditor->scratchBuffer != NULL)
      {
         ExFreePoolWithTag(
            streamEditor->scratchBuffer, 
            STREAM_EDITOR_FLAT_BUFFER_TAG
            );

         streamEditor->scratchBuffer = NULL;
         streamEditor->bufferSize = 0;
         streamEditor->dataLength = 0;
      }

      if (newBuffer != NULL)
      {
         streamEditor->scratchBuffer = newBuffer;
         streamEditor->bufferSize = newBufferSize;
         streamEditor->dataLength = existingDataLength;
      }
      else
      {
         return FALSE;
      }
   }

   FwpsCopyStreamDataToBuffer(
      streamData,
      (BYTE*)streamEditor->scratchBuffer + streamEditor->dataLength,
      streamData->dataLength,
      &bytesCopied
      );

   ASSERT(bytesCopied == streamData->dataLength);

   streamEditor->dataLength += bytesCopied;

   return TRUE;
}

NTSTATUS DeviceControlFunction(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PIO_STACK_LOCATION  irpSp;
    NTSTATUS            ntStatus = STATUS_SUCCESS;
	//__asm int 3

	//irpSp = IoGetCurrentIrpStackLocation(Irp);

	RtlCopyMemory( pointer, Irp->AssociatedIrp.SystemBuffer, sizeof(pointer));

	Irp->IoStatus.Status = ntStatus;

    IoCompleteRequest( Irp, IO_NO_INCREMENT );

    return ntStatus;
}

 NTSTATUS CreateCloseFunction(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest( Irp, IO_NO_INCREMENT );

	return STATUS_SUCCESS;
} 




