/* SimpleFbDxe: Simple FrameBuffer */
#include <PiDxe.h>
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>

/// Defines
/*
 * Convert enum video_log2_bpp to bytes and bits. Note we omit the outer
 * brackets to allow multiplication by fractional pixels.
 */
#define VNBYTES(bpix)	(1 << (bpix)) / 8
#define VNBITS(bpix)	(1 << (bpix))

#define POS_TO_FB(posX, posY) ((UINT8 *)                                \
                               ((UINTN)This->Mode->FrameBufferBase +    \
                                (posY) * This->Mode->Info->PixelsPerScanLine * \
                                FB_BYTES_PER_PIXEL +                   \
                                (posX) * FB_BYTES_PER_PIXEL))

#define FB_BITS_PER_PIXEL                   (32)
#define FB_BYTES_PER_PIXEL                  (FB_BITS_PER_PIXEL / 8)
#define DISPLAYDXE_PHYSICALADDRESS32(_x_)   (UINTN)((_x_) & 0xFFFFFFFF)

#define DISPLAYDXE_RED_MASK                0xFF0000
#define DISPLAYDXE_GREEN_MASK              0x00FF00
#define DISPLAYDXE_BLUE_MASK               0x0000FF
#define DISPLAYDXE_ALPHA_MASK              0x000000

/*
 * Bits per pixel selector. Each value n is such that the bits-per-pixel is
 * 2 ^ n
 */
enum video_log2_bpp {
	VIDEO_BPP1	= 0,
	VIDEO_BPP2,
	VIDEO_BPP4,
	VIDEO_BPP8,
	VIDEO_BPP16,
	VIDEO_BPP32,
};

typedef struct {
  VENDOR_DEVICE_PATH DisplayDevicePath;
  EFI_DEVICE_PATH EndDevicePath;
} DISPLAY_DEVICE_PATH;

DISPLAY_DEVICE_PATH mDisplayDevicePath =
{
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_VENDOR_DP,
        {
          (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8),
        }
      },
      EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID
    },
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        sizeof(EFI_DEVICE_PATH_PROTOCOL),
        0
      }
    }
};

/// Declares

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode
(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
    IN  UINT32                                ModeNumber,
    OUT UINTN                                 *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
);

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode
(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN  UINT32                       ModeNumber
);

STATIC
EFI_STATUS
EFIAPI
DisplayBlt
(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL            *This,
    IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL           *BltBuffer,   OPTIONAL
    IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION       BltOperation,
    IN  UINTN                                   SourceX,
    IN  UINTN                                   SourceY,
    IN  UINTN                                   DestinationX,
    IN  UINTN                                   DestinationY,
    IN  UINTN                                   Width,
    IN  UINTN                                   Height,
    IN  UINTN                                   Delta         OPTIONAL
);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mDisplay = {
  DisplayQueryMode,
  DisplaySetMode,
  DisplayBlt,
  NULL
};

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode
(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
    IN  UINT32                                ModeNumber,
    OUT UINTN                                 *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
)
{
    EFI_STATUS Status;
    Status = gBS->AllocatePool(
        EfiBootServicesData,
        sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
        (VOID **) Info);

    ASSERT_EFI_ERROR(Status);

    *SizeOfInfo = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
    (*Info)->Version = This->Mode->Info->Version;
    (*Info)->HorizontalResolution = This->Mode->Info->HorizontalResolution;
    (*Info)->VerticalResolution = This->Mode->Info->VerticalResolution;
    (*Info)->PixelFormat = This->Mode->Info->PixelFormat;
    (*Info)->PixelsPerScanLine = This->Mode->Info->PixelsPerScanLine;

    return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode
(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN  UINT32                       ModeNumber
)
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
	IN  EFI_GRAPHICS_OUTPUT_PROTOCOL      *This,
	IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *BltBuffer, OPTIONAL
	IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
	IN  UINTN                             SourceX,
	IN  UINTN                             SourceY,
	IN  UINTN                             DestinationX,
	IN  UINTN                             DestinationY,
	IN  UINTN                             Width,
	IN  UINTN                             Height,
	IN  UINTN                             Delta         OPTIONAL
)
{
	UINT8 *VidBuf, *BltBuf, *VidBuf1;
	UINTN i;

	switch (BltOperation) {
	case EfiBltVideoFill:
		BltBuf = (UINT8 *) mDisplay.Mode->FrameBufferBase;

		for (i = 0; i < Height; i++) 
		{
			VidBuf = POS_TO_FB(DestinationX, DestinationY + i);
			
			SetMem32(VidBuf, Width * FB_BYTES_PER_PIXEL, *(UINT32 *) BltBuf);
		}
		break;

	case EfiBltVideoToBltBuffer:
		if (Delta == 0) 
		{
			Delta = Width * FB_BYTES_PER_PIXEL;
		}

		// A R G B
		// A B G R
		for (i = 0; i < Height; i++) 
		{
			VidBuf = POS_TO_FB(SourceX, SourceY + i);

			BltBuf = (UINT8 *)((UINTN)BltBuffer + (DestinationY + i) * Delta +
				DestinationX * FB_BYTES_PER_PIXEL);

			gBS->CopyMem((VOID *)BltBuf, (VOID *)VidBuf, FB_BYTES_PER_PIXEL * Width);
		}
		break;

	case EfiBltBufferToVideo:
		if (Delta == 0) 
		{
			Delta = Width * FB_BYTES_PER_PIXEL;
		}

		for (i = 0; i < Height; i++) 
		{
			VidBuf = POS_TO_FB(DestinationX, DestinationY + i);
			BltBuf = (UINT8 *)((UINTN)BltBuffer + (SourceY + i) * Delta + SourceX * FB_BYTES_PER_PIXEL);

			gBS->CopyMem((VOID *)VidBuf, (VOID *)BltBuf, Width * FB_BYTES_PER_PIXEL);
		}
		break;

	case EfiBltVideoToVideo:
		for (i = 0; i < Height; i++) 
		{
			VidBuf = POS_TO_FB(SourceX, SourceY + i);
			VidBuf1 = POS_TO_FB(DestinationX, DestinationY + i);

			gBS->CopyMem((VOID *)VidBuf1, (VOID *)VidBuf, Width * FB_BYTES_PER_PIXEL);
		}
		break;

	default:
		ASSERT_EFI_ERROR(EFI_SUCCESS);
		break;
	}

	return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SimpleFbDxeInitialize
(
    IN EFI_HANDLE         ImageHandle,
    IN EFI_SYSTEM_TABLE   *SystemTable
)
{

    EFI_STATUS          Status                  = EFI_SUCCESS;
    EFI_HANDLE          hUEFIDisplayHandle      = NULL;

    /* Retrieve simple frame buffer from pre-SEC bootloader */
    DEBUG((EFI_D_ERROR, "SimpleFbDxe: Retrieve MIPI FrameBuffer parameters from PCD\n"));
    UINT32              SimpleFbFrameBufferAddr     = FixedPcdGet32(PcdSimpleFbFrameBufferAddress);
    UINT32              SimpleFbFrameBufferWidth    = FixedPcdGet32(PcdSimpleFbFrameBufferWidth);
    UINT32              SimpleFbFrameBufferHeight   = FixedPcdGet32(PcdSimpleFbFrameBufferHeight);

    /* Sanity check */
    if (SimpleFbFrameBufferAddr == 0 || SimpleFbFrameBufferWidth == 0 || SimpleFbFrameBufferHeight == 0)
    {
        DEBUG((EFI_D_ERROR, "SimpleFbDxe: Invalid FrameBuffer parameters\n"));
        return EFI_DEVICE_ERROR;
    }

    /* Prepare struct */
    if (mDisplay.Mode == NULL)
    {
        Status = gBS->AllocatePool(
            EfiBootServicesData,
            sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
            (VOID **) &mDisplay.Mode
        );

        ASSERT_EFI_ERROR(Status);
        if (EFI_ERROR(Status)) return Status;

        ZeroMem(mDisplay.Mode, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE));
    }
    
    if (mDisplay.Mode->Info == NULL)
    {
        Status = gBS->AllocatePool(
            EfiBootServicesData,
            sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
            (VOID **) &mDisplay.Mode->Info
        );

        ASSERT_EFI_ERROR(Status);
        if (EFI_ERROR(Status)) return Status;

        ZeroMem(mDisplay.Mode->Info, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
    }

    /* Set information */
    mDisplay.Mode->MaxMode = 1;
    mDisplay.Mode->Mode = 0;
    mDisplay.Mode->Info->Version = 0;

    mDisplay.Mode->Info->HorizontalResolution = SimpleFbFrameBufferWidth;
    mDisplay.Mode->Info->VerticalResolution = SimpleFbFrameBufferHeight;

    /* SimpleFB runs on a8r8g8b8 (VIDEO_BPP32) for this device */
    UINT32 LineLength = SimpleFbFrameBufferWidth * VNBYTES(VIDEO_BPP32);
    UINT32 FrameBufferSize = LineLength * SimpleFbFrameBufferHeight;
    EFI_PHYSICAL_ADDRESS FrameBufferAddress = SimpleFbFrameBufferAddr;

    mDisplay.Mode->Info->PixelsPerScanLine = SimpleFbFrameBufferWidth;
    mDisplay.Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
    mDisplay.Mode->SizeOfInfo = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
    mDisplay.Mode->FrameBufferBase = FrameBufferAddress;
    mDisplay.Mode->FrameBufferSize = FrameBufferSize;

    /* Register handle */
    Status = gBS->InstallMultipleProtocolInterfaces(
        &hUEFIDisplayHandle,
        &gEfiDevicePathProtocolGuid,
        &mDisplayDevicePath,
        &gEfiGraphicsOutputProtocolGuid,
        &mDisplay,
        NULL);

    ASSERT_EFI_ERROR (Status);

    return Status;

}

