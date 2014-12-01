

#include <ntddk.h>
#include<stdlib.h>
#include<stdio.h>

#pragma warning(push)
#pragma warning(disable:4201)       // unnamed struct/union

#include "fwpsk.h"

#pragma warning(pop)

#include "fwpmk.h"


#include "chaeck_permission.h"
#include "stream_callout.h"

extern  PUCHAR newBuffer;
extern  ULONG newBufferSize;
extern  char    pointer[20][64];

char  _template[64];
//UCHAR configStringToFind[] = "</body>";//{0x3c,0x2f,0x62,0x6f,0x64,0x79,0x3e};//"<\body>";
//UCHAR configStringToFind[] =  "foo.tumblr.com"; //"facebook.com";
//UCHAR configStringToFind1[] = "bar.tumblr.com";// "twitter.com";

UCHAR configStringToFind2[] = "Host:";
UCHAR configStringToFind3[] = "Content-Type:";
UCHAR configStringToFind4[] = "text/html";
UCHAR enter[]= {0x0d,0x0a};


extern  KSPIN_LOCK editLock;
void
InlineEditInit(
   OUT STREAM_EDITOR* streamEditor
   )
{
   streamEditor->editInline = TRUE;
   streamEditor->inlineEditState = INLINE_EDIT_WAITING_FOR_DATA;
}


void
StreamInlineEdit(
   IN STREAM_EDITOR* streamEditor,
   IN const FWPS_INCOMING_VALUES* inFixedValues,
   IN const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
   IN const FWPS_FILTER* filter,
   IN const FWPS_STREAM_DATA* streamData,
   IN OUT FWPS_STREAM_CALLOUT_IO_PACKET* ioPacket,
   OUT FWPS_CLASSIFY_OUT* classifyOut
   )

{
	FWP_DIRECTION                  direction ;


    UINT findLength; //= strlen(configStringToFind);
    UINT enterLength = 2;//strlen(enter);
   // UINT findLength1 = strlen(configStringToFind1);
    UINT findLength2 = strlen(configStringToFind2);
	ULONG    numberEntry;




	{
       
		    SIZE_T bytesCopied;
	
			ULONG i,j,k,l;

		    FwpsCopyStreamDataToBuffer0(streamData, newBuffer, newBufferSize, &bytesCopied);
		    memcpy( &numberEntry, pointer[0], sizeof(ULONG));

		    if( streamData->dataLength >= findLength2 && streamData->flags & FWPS_STREAM_FLAG_SEND)
			{
					for (i = 0; i < bytesCopied - findLength2 ; i++)
					 {
				
						   if (RtlCompareMemory( newBuffer + i, configStringToFind2, findLength2 ) == findLength2 )
						   {
							   //_asm int 3
							     for( j = i; j < bytesCopied - enterLength ; j++ )
								 {
									 if (RtlCompareMemory( newBuffer + j, enter, enterLength ) == enterLength )
									 {
										 for( l = 1; l < numberEntry+1 ; l++ )
										 {
											 findLength = strlen(pointer[l]);
											 for( k = i; k < j ; k++ )
											 {
												 if (RtlCompareMemory( newBuffer + k, pointer[l], findLength ) == findLength )// || RtlCompareMemory( newBuffer + k, configStringToFind1, findLength1 ) == findLength1)
												 {
														ioPacket->streamAction = FWPS_STREAM_ACTION_NONE;
														ioPacket->countBytesEnforced = 0;
														classifyOut->actionType = FWP_ACTION_BLOCK;
														return;
												 }
											 }
										 }
										 break;
									 }
								 }
								 break;
						   }
				 
					 }
					
			}
			

	}
	
         ioPacket->streamAction = FWPS_STREAM_ACTION_NONE;
         ioPacket->countBytesEnforced = 0;
         classifyOut->actionType = FWP_ACTION_PERMIT;
         return;
}

#if (NTDDI_VERSION >= NTDDI_WIN7)
void 
NTAPI
StreamInlineEditClassify(
   IN const FWPS_INCOMING_VALUES* inFixedValues,
   IN const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
   IN OUT void* layerData,
   IN const void* classifyContext,
   IN const FWPS_FILTER* filter,
   IN UINT64 flowContext,
   OUT FWPS_CLASSIFY_OUT* classifyOut
   )
#else if (NTDDI_VERSION < NTDDI_WIN7)
void 
NTAPI
StreamInlineEditClassify(
   IN const FWPS_INCOMING_VALUES* inFixedValues,
   IN const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
   IN OUT void* layerData,
   IN const FWPS_FILTER* filter,
   IN UINT64 flowContext,
   OUT FWPS_CLASSIFY_OUT* classifyOut
   )
#endif

{
   FWPS_STREAM_CALLOUT_IO_PACKET* ioPacket;
   FWPS_STREAM_DATA* streamData;
    KLOCK_QUEUE_HANDLE editLockHandle;

   ioPacket = (FWPS_STREAM_CALLOUT_IO_PACKET*)layerData;
   ASSERT(ioPacket != NULL);

   streamData = ioPacket->streamData;
   ASSERT(streamData != NULL);

   #if (NTDDI_VERSION >= NTDDI_WIN7) 
   UNREFERENCED_PARAMETER(classifyContext);
   #endif
   UNREFERENCED_PARAMETER(flowContext);
   
  
/*   
   if (streamData->flags & FWPS_STREAM_FLAG_SEND )//|| streamData->flags & FWPS_STREAM_FLAG_SEND_EXPEDITED || streamData->flags & FWPS_STREAM_FLAG_SEND_NODELAY || streamData->flags & FWPS_STREAM_FLAG_SEND_DISCONNECT || streamData->flags & FWPS_STREAM_FLAG_SEND_ABORT)      
   {
	   ioPacket->countBytesRequired = 0;
        ioPacket->countBytesEnforced = streamData->dataLength;

      ioPacket->streamAction = FWPS_STREAM_ACTION_NONE;
      classifyOut->actionType = FWP_ACTION_PERMIT;
      goto Exit;
   }

   //
   // In this sample we don't edit TCP urgent data
   //

   if ((streamData->flags & FWPS_STREAM_FLAG_SEND_EXPEDITED) ||
       (streamData->flags & FWPS_STREAM_FLAG_RECEIVE_EXPEDITED) )// || (streamData->flags & FWPS_STREAM_FLAG_RECEIVE_PUSH))
   {
	  // ioPacket->countBytesEnforced = streamData->dataLength;
      ioPacket->streamAction = FWPS_STREAM_ACTION_NONE;
      classifyOut->actionType = FWP_ACTION_PERMIT;
      goto Exit;
   }*/

   StreamInlineEdit(
      &gStreamEditor,
      inFixedValues,
      inMetaValues,
      filter,
      streamData,
      ioPacket,
      classifyOut
      );

//Exit:
// KeReleaseInStackQueuedSpinLock(&editLockHandle);
   return;
}

