// AacDecoder.cpp: CAacDecoder �N���X�̃C���v�������e�[�V����
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "AacDecoder.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


// "Code from FAAD2 is copyright (c) Nero AG, www.nero.com"
#pragma comment(lib, "LibFaad.lib")


//////////////////////////////////////////////////////////////////////
// �\�z/����
//////////////////////////////////////////////////////////////////////

CAacDecoder::CAacDecoder()
	: m_AdtsParser(NULL)
	, m_hDecoder(NULL)
	, m_bInitRequest(false)
	, m_LastChannelConfig(0xFF)
	, m_pAdtsFrame(NULL)
	, m_bDecodeError(false)
{
}


CAacDecoder::~CAacDecoder()
{
	Close();
}


bool CAacDecoder::Open()
{
	if (!OpenDecoder())
		return false;

	m_AdtsParser.Reset();
	ClearAudioInfo();

	return true;
}


void CAacDecoder::Close()
{
	CloseDecoder();
	m_AdtsParser.Reset();
	m_pAdtsFrame = NULL;
}


bool CAacDecoder::IsOpened() const
{
	return m_hDecoder != NULL;
}


bool CAacDecoder::Reset()
{
	if (!ResetDecoder())
		return false;

	m_AdtsParser.Reset();
	ClearAudioInfo();
	m_bDecodeError = false;

	return true;
}


bool CAacDecoder::Decode(const BYTE *pData, DWORD *pDataSize, DecodeFrameInfo *pInfo)
{
	if (m_hDecoder == NULL)
		return false;

	m_pAdtsFrame = NULL;

	CAdtsFrame *pFrame;
	if (!m_AdtsParser.StoreEs(pData, pDataSize, &pFrame))
		return false;

	if (!DecodeFrame(pFrame, pInfo)) {
		m_bDecodeError = true;
		return false;
	}

	m_pAdtsFrame = pFrame;
	m_bDecodeError = false;

	return true;
}


bool CAacDecoder::GetSpdifFrameInfo(SpdifFrameInfo *pInfo) const
{
	if (pInfo == NULL || m_pAdtsFrame == NULL)
		return false;

	pInfo->Pc = 0x0007;	// MPEG-2 AAC ADTS
	pInfo->FrameSize = m_pAdtsFrame->GetFrameLength();
	pInfo->SamplesPerFrame = 1024;

	return true;
}


int CAacDecoder::GetSpdifBurstPayload(BYTE *pBuffer, DWORD BufferSize) const
{
	if (pBuffer == NULL || m_pAdtsFrame == NULL)
		return 0;

	if (m_pAdtsFrame->GetRawDataBlockNum() != 0) {
		TRACE(TEXT("Invalid no_raw_data_blocks_in_frame (%d)\r\n"),
			  m_pAdtsFrame->GetRawDataBlockNum());
		return 0;
	}

	const int FrameSize = m_pAdtsFrame->GetFrameLength();
	const int DataBurstSize = (FrameSize + 1) & ~1;
	if (BufferSize < (DWORD)DataBurstSize)
		return 0;

	_swab(pointer_cast<char*>(const_cast<BYTE*>(m_pAdtsFrame->GetData())),
		  pointer_cast<char*>(pBuffer), FrameSize & ~1);
	if (FrameSize & 1) {
		pBuffer[FrameSize - 1] = 0;
		pBuffer[FrameSize] = m_pAdtsFrame->GetAt(FrameSize - 1);
	}

	return DataBurstSize;
}


bool CAacDecoder::GetChannelMap(int Channels, int *pMap) const
{
	switch (Channels) {
	case 2:
		pMap[CHANNEL_2_L] = 0;
		pMap[CHANNEL_2_R] = 1;
		break;

	case 6:
		pMap[CHANNEL_6_FL]  = 1;
		pMap[CHANNEL_6_FR]  = 2;
		pMap[CHANNEL_6_FC]  = 0;
		pMap[CHANNEL_6_LFE] = 5;
		pMap[CHANNEL_6_BL]  = 3;
		pMap[CHANNEL_6_BR]  = 4;
		break;

	default:
		return false;
	}

	return true;
}


bool CAacDecoder::GetDownmixInfo(DownmixInfo *pInfo) const
{
	if (pInfo == NULL)
		return false;

	// 5.1ch�_�E���~�b�N�X�ݒ�
	/*
	�{���̌v�Z�� (STD-B21 6.2)
	����������������������������������������������������������
	��*1��*2  ��*3��k�̒l ���v�Z��(*4)                      ��
	����������������������������������������������������������
	�� 1�� 0/1�� 0��1/��2 ��Set1                            ��
	��  ��    �� 1��1/2   ��Lt=a*(L+1/��2*C�{k*Sl)          ��
	��  ��    �� 2��1/2��2��Rt=a*(R+1/��2*C�{k*Sr)          ��
	��  ��    �� 3��0     ��a=1/��2                         ��
	����������������������������������������������������������
	�� 0��    ��  ��      ��Set3                            ��
	��  ��    ��  ��      ��Lt=(1/��2)*(L+1/��2*C�{1/��2*Sl)��
	��  ��    ��  ��      ��Rt=(1/��2)*(R+1/��2*C�{1/��2*Sr)��
	����������������������������������������������������������
	*1 matrix_mixdown_idx_present
	*2 pseudo_surround_enable
	*3 matrix_mixdown_idx
	*4 L=Left, R=Right, C=Center, Sl=Rear left, Sr=Rear right

	�K�v�Ȓl�� NeAACDecStruct.pce (NeAACDecStruct* = NeAACDecHandle) �ŎQ�Ƃł��邪�A
	���̂Ƃ��낻���܂ł��Ă��Ȃ��B
	*/

	static const double PSQR = 1.0 / 1.4142135623730950488016887242097;

	pInfo->Center = PSQR;
	pInfo->Front  = 1.0;
	pInfo->Rear   = PSQR;
	pInfo->LFE    = PSQR;

	return true;
}


bool CAacDecoder::OpenDecoder()
{
	CloseDecoder();

	// FAAD2�I�[�v��
	m_hDecoder = ::NeAACDecOpen();
	if (m_hDecoder == NULL)
		return false;

	// �f�t�H���g�ݒ�擾
	NeAACDecConfigurationPtr pDecodeConfig = ::NeAACDecGetCurrentConfiguration(m_hDecoder);

	// �f�R�[�_�ݒ�
	pDecodeConfig->defObjectType = LC;
	pDecodeConfig->defSampleRate = 48000UL;
	pDecodeConfig->outputFormat = FAAD_FMT_16BIT;
	pDecodeConfig->downMatrix = 0;
	pDecodeConfig->useOldADTSFormat = 0;

	if (!::NeAACDecSetConfiguration(m_hDecoder, pDecodeConfig)) {
		Close();
		return false;
	}

	m_bInitRequest = true;
	m_LastChannelConfig = 0xFF;
	m_bDecodeError = false;

	return true;
}


void CAacDecoder::CloseDecoder()
{
	// FAAD2�N���[�Y
	if (m_hDecoder) {
		::NeAACDecClose(m_hDecoder);
		m_hDecoder = NULL;
	}
}


bool CAacDecoder::ResetDecoder()
{
	if (m_hDecoder == NULL)
		return false;

	return OpenDecoder();
}


bool CAacDecoder::DecodeFrame(const CAdtsFrame *pFrame, DecodeFrameInfo *pInfo)
{
	if (m_hDecoder == NULL) {
		return false;
	}

	// ����t���[�����
	if (m_bInitRequest || pFrame->GetChannelConfig() != m_LastChannelConfig) {
		if (!m_bInitRequest) {
			// �`�����l���ݒ肪�ω������A�f�R�[�_���Z�b�g
			if (!ResetDecoder())
				return false;
		}

		unsigned long SampleRate;
		unsigned char Channels;
		if (::NeAACDecInit(m_hDecoder,
				const_cast<BYTE*>(pFrame->GetData()), pFrame->GetSize(),
				&SampleRate, &Channels) < 0) {
			return false;
		}

		m_bInitRequest = false;
		m_LastChannelConfig = pFrame->GetChannelConfig();
	}

	// �f�R�[�h
	NeAACDecFrameInfo FrameInfo;
	//::ZeroMemory(&FrameInfo, sizeof(FrameInfo));

	BYTE *pPcmBuffer = pointer_cast<BYTE*>(
		::NeAACDecDecode(m_hDecoder, &FrameInfo,
						 const_cast<BYTE*>(pFrame->GetData()), pFrame->GetSize()));

	bool bOK = false;

	if (FrameInfo.error == 0) {
		m_AudioInfo.Frequency = FrameInfo.samplerate;
		m_AudioInfo.Channels = FrameInfo.channels;
		// FAAD�ł̓��m������2ch�Ƀf�R�[�h�����
		if (FrameInfo.channels == 2 && m_LastChannelConfig == 1)
			m_AudioInfo.OrigChannels = 1;
		else
			m_AudioInfo.OrigChannels = FrameInfo.channels;
		m_AudioInfo.bDualMono = FrameInfo.channels == 2 && m_LastChannelConfig == 0;
		if (FrameInfo.samples > 0) {
			pInfo->pData = pPcmBuffer;
			pInfo->Samples = FrameInfo.samples / FrameInfo.channels;
			pInfo->Info = m_AudioInfo;
			pInfo->bDiscontinuity = m_bDecodeError;
			bOK = true;
		}
	} else {
		// �G���[����
#ifdef _DEBUG
		::OutputDebugString(TEXT("CAacDecoder::Decode error - "));
		::OutputDebugStringA(NeAACDecGetErrorMessage(FrameInfo.error));
		::OutputDebugString(TEXT("\n"));
#endif
		// ���Z�b�g����
		ResetDecoder();
	}

	return bOK;
}