#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

extern "C" {
#include "aacdec.h"
#include "aaccommon.h"
#include "coder.h"

int DecodeNextElement(AACDecInfo *aacDecInfo, unsigned char **buf, int *bitOffset, int *bitsAvail) { return 0; }
int DecodeNoiselessData(AACDecInfo *aacDecInfo, unsigned char **buf, int *bitOffset, int *bitsAvail, int ch) { return 0; }
int DecodeProgramConfigElement(ProgConfigElement *pce, BitStreamInfo *bsi) { return 0; }
void ClearBuffer(void *buf, int nBytes) { memset(buf, 0, nBytes); }
}

static void build_adts_header(unsigned char *hdr, int profile, int sampRateIdx, int channelConfig, int frameLength, int numRawDataBlocks) {
    memset(hdr, 0, 7);
    hdr[0] = 0xFF;
    hdr[1] = 0xF1; // Sync 0xFFF, ID=1 (MPEG4), Layer=0, ProtectBit=1 (no CRC)
    hdr[2] = ((profile & 0x03) << 6) | ((sampRateIdx & 0x0F) << 2) | ((channelConfig >> 2) & 0x01);
    hdr[3] = ((channelConfig & 0x03) << 6) | ((frameLength >> 11) & 0x03);
    hdr[4] = (frameLength >> 3) & 0xFF;
    hdr[5] = ((frameLength & 0x07) << 5) | 0x1F; // buffer full = 0x7FF
    hdr[6] = 0xFC | ((numRawDataBlocks - 1) & 0x03);
}

int main() {
    printf("Starting AAC filefmt unit test...\n");

    AACDecInfo aacDecInfo;
    PSInfoBase psi;
    memset(&aacDecInfo, 0, sizeof(AACDecInfo));
    memset(&psi, 0, sizeof(PSInfoBase));
    aacDecInfo.psInfoBase = &psi;

    // Test 1: Standard Stereo ADTS frame (44.1kHz, channelConfig=2, frameLength=400, 1 raw block)
    {
        unsigned char hdr[16];
        build_adts_header(hdr, 1, 4, 2, 400, 1); // profile=1 (LC), sampRateIdx=4 (44100), channelConfig=2 (Stereo)
        unsigned char *buf = hdr;
        int bitOffset = 0;
        int bitsAvail = 7 * 8;

        int err = UnpackADTSHeader(&aacDecInfo, &buf, &bitOffset, &bitsAvail);
        assert(err == ERR_AAC_NONE);
        assert(aacDecInfo.nChans == 2);
        assert(aacDecInfo.sampRate == 44100);
        // Bitrate calculation: (400 * 8 * 44100) / 1024 = 137812
        assert(aacDecInfo.bitRate == 137812);
        printf("  [PASS] Test 1: Standard Stereo ADTS header (bitRate=%d bps, nChans=%d, sampRate=%d Hz)\n",
               aacDecInfo.bitRate, aacDecInfo.nChans, aacDecInfo.sampRate);
    }

    // Test 2: 5.1 Surround ADTS frame (48kHz, channelConfig=6, frameLength=600, 1 raw block)
    {
        unsigned char hdr[16];
        build_adts_header(hdr, 1, 3, 6, 600, 1); // sampRateIdx=3 (48000), channelConfig=6 (5.1)
        unsigned char *buf = hdr;
        int bitOffset = 0;
        int bitsAvail = 7 * 8;

        int err = UnpackADTSHeader(&aacDecInfo, &buf, &bitOffset, &bitsAvail);
        assert(err == ERR_AAC_NONE);
        assert(aacDecInfo.nChans == 6);
        assert(aacDecInfo.sampRate == 48000);
        // Bitrate calculation: (600 * 8 * 48000) / 1024 = 225000
        assert(aacDecInfo.bitRate == 225000);
        printf("  [PASS] Test 2: 5.1 Surround ADTS header (bitRate=%d bps, nChans=%d, sampRate=%d Hz)\n",
               aacDecInfo.bitRate, aacDecInfo.nChans, aacDecInfo.sampRate);
    }

    // Test 3: Implicit Channel Config (channelConfig=0)
    {
        unsigned char hdr[16];
        build_adts_header(hdr, 1, 4, 0, 350, 1); // channelConfig=0 (Implicit)
        unsigned char *buf = hdr;
        int bitOffset = 0;
        int bitsAvail = 7 * 8;

        int err = UnpackADTSHeader(&aacDecInfo, &buf, &bitOffset, &bitsAvail);
        assert(err == ERR_AAC_NONE);
        assert(aacDecInfo.nChans == -1); // Requires implicit channel mapping
        assert(aacDecInfo.bitRate == (int)(((long long)350 * 8 * 44100) / 1024));
        printf("  [PASS] Test 3: Implicit Channel Config ADTS header (nChans=%d, bitRate=%d bps)\n",
               aacDecInfo.nChans, aacDecInfo.bitRate);
    }

    // Test 4: Switching from implicit channel config back to explicit channel config (channelConfig=1 Mono)
    {
        unsigned char hdr[16];
        build_adts_header(hdr, 1, 4, 1, 200, 1); // channelConfig=1 (Mono)
        unsigned char *buf = hdr;
        int bitOffset = 0;
        int bitsAvail = 7 * 8;

        int err = UnpackADTSHeader(&aacDecInfo, &buf, &bitOffset, &bitsAvail);
        assert(err == ERR_AAC_NONE);
        assert(aacDecInfo.nChans == 1);
        printf("  [PASS] Test 4: Channel Config switch to explicit mono (nChans=%d)\n", aacDecInfo.nChans);
    }

    printf("All AAC filefmt unit tests PASSED!\n");
    return 0;
}
