/*
 * Copyright (c) 2010-2016 Stephane Poirier
 *
 * stephane.poirier@oifii.org
 *
 * Stephane Poirier
 * 3532 rue Ste-Famille, #3
 * Montreal, QC, H2X 2L1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <math.h>
#include "bass.h"
#include "bassasio.h"

#include <string>
using namespace std;
#include <assert.h>
#include <map>
map<string,int> global_asiodevicemap;
string global_audiodevicename; 
int global_inputAudioChannelSelectors[2];
int global_outputAudioChannelSelectors[2];
int global_deviceid=0;

HWND win=NULL;

#define MESS(id,m,w,l) SendDlgItemMessage(win,id,m,(WPARAM)w,(LPARAM)l)

HSTREAM fxchan;	// FX stream
HFX fx[4]={0};	// FX handles
int input=0;	// current input source

void Error(const char *es)
{
	char mes[200];
	sprintf(mes,"%s\n(error code: %d/%d)",es,BASS_ErrorGetCode(),BASS_ASIO_ErrorGetCode());
	MessageBox(win,mes,"Error",0);
}

DWORD CALLBACK AsioProc(BOOL isinput, DWORD channel, void *buffer, DWORD length, void *user)
{
	static float buf[100000]; // input buffer - 100000 should be enough :)
	if (isinput) 
	{
		memcpy(buf,buffer,length);
	} 
	else 
	{
		memcpy(buffer,buf,length);
		BASS_ChannelGetData(fxchan,buffer,length); // apply FX
	}
	return length;
}


static BOOL Initialize()
{
	// not playing anything via BASS, so don't need an update thread
	BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD,0);
	// init BASS (for the FX)
	BASS_Init(0,44100,0,0,NULL);

	// init ASIO - first device
	//if (!BASS_ASIO_Init(0,0)) 
	//if (!BASS_ASIO_Init(3,0)) //device id 3 is "E-MU ASIO for spi"
	if (!BASS_ASIO_Init(global_deviceid,0)) //device id 3 is "E-MU ASIO for spi"
	{
		BASS_Free();
		Error("Can't initialize ASIO");
		return FALSE;
	}
	input=global_inputAudioChannelSelectors[0]/2; //input is a asio channel pair
	{ // get list of inputs (assuming channels are all ordered in left/right pairs)
		int c;
		BASS_ASIO_CHANNELINFO i,i2;
		for (c=0;BASS_ASIO_ChannelGetInfo(TRUE,c,&i);c+=2) 
		{
			char name[200];
			if (!BASS_ASIO_ChannelGetInfo(TRUE,c+1,&i2)) break; // no "right" channel
			_snprintf(name,sizeof(name),"%s + %s",i.name,i2.name);
			MESS(10,CB_ADDSTRING,0,name);
			BASS_ASIO_ChannelJoin(TRUE,c+1,c); // join the pair of channels
		}
		MESS(10,CB_SETCURSEL,input,0);
	}

	// create a dummy stream to apply FX
	fxchan=BASS_StreamCreate(BASS_ASIO_GetRate(),2,BASS_SAMPLE_FLOAT|BASS_STREAM_DECODE,STREAMPROC_DUMMY,0);

	// enable first inputs
	BASS_ASIO_ChannelEnable(TRUE,input,&AsioProc,0);
	// enable first outputs
	//BASS_ASIO_ChannelEnable(FALSE,0,&AsioProc,0);
	BASS_ASIO_ChannelEnable(FALSE,global_outputAudioChannelSelectors[0],&AsioProc,0);
	//BASS_ASIO_ChannelJoin(FALSE,1,0);
	BASS_ASIO_ChannelJoin(FALSE,global_outputAudioChannelSelectors[1],global_outputAudioChannelSelectors[0]);
	// set input and output to floating-point
	BASS_ASIO_ChannelSetFormat(TRUE,input,BASS_ASIO_FORMAT_FLOAT);
	//BASS_ASIO_ChannelSetFormat(FALSE,0,BASS_ASIO_FORMAT_FLOAT);
	BASS_ASIO_ChannelSetFormat(FALSE,global_outputAudioChannelSelectors[0],BASS_ASIO_FORMAT_FLOAT);
	// start with output volume at 0 (in case of nasty feedback)
	//BASS_ASIO_ChannelSetVolume(FALSE,0,0);
	//BASS_ASIO_ChannelSetVolume(FALSE,1,0);
	BASS_ASIO_ChannelSetVolume(FALSE,global_outputAudioChannelSelectors[0],0);
	BASS_ASIO_ChannelSetVolume(FALSE,global_outputAudioChannelSelectors[1],0);
	// start it (using default buffer size)
	if (!BASS_ASIO_Start(0)) 
	{
		BASS_ASIO_Free();
		BASS_Free();
		Error("Can't initialize recording device");
		return FALSE;
	}

	{ // display total (input+output) latency
		char buf[20];
		sprintf(buf,"%.1fms",(BASS_ASIO_GetLatency(FALSE)+BASS_ASIO_GetLatency(TRUE))*1000/BASS_ASIO_GetRate());
		MESS(15,WM_SETTEXT,0,buf);
	}

	return TRUE;
}

BOOL CALLBACK dialogproc(HWND h,UINT m,WPARAM w,LPARAM l)
{
	switch (m) 
	{
		case WM_COMMAND:
			switch (LOWORD(w)) 
			{
				case IDCANCEL:
					DestroyWindow(h);
					return 1;
				case 10:
					if (HIWORD(w)==CBN_SELCHANGE) 
					{ // input selection changed
						BASS_ASIO_Stop(); // stop ASIO processing
						BASS_ASIO_ChannelEnable(TRUE,input,NULL,0); // disable old inputs
						input=MESS(10,CB_GETCURSEL,0,0)*2; // get the selection
						BASS_ASIO_ChannelEnable(TRUE,input,&AsioProc,0); // enable new inputs
						BASS_ASIO_ChannelSetFormat(TRUE,input,BASS_ASIO_FORMAT_FLOAT);
						BASS_ASIO_Start(0); // resume ASIO processing
					}
					return 1;
				case 20: // toggle chorus
					if (fx[0]) 
					{
						BASS_ChannelRemoveFX(fxchan,fx[0]);
						fx[0]=0;
					} else
						fx[0]=BASS_ChannelSetFX(fxchan,BASS_FX_DX8_CHORUS,0);
					return 1;
				case 21: // toggle gargle
					if (fx[1]) 
					{
						BASS_ChannelRemoveFX(fxchan,fx[1]);
						fx[1]=0;
					} else
						fx[1]=BASS_ChannelSetFX(fxchan,BASS_FX_DX8_GARGLE,0);
					return 1;
				case 22: // toggle reverb
					if (fx[2]) 
					{
						BASS_ChannelRemoveFX(fxchan,fx[2]);
						fx[2]=0;
					} else
						fx[2]=BASS_ChannelSetFX(fxchan,BASS_FX_DX8_REVERB,0);
					return 1;
				case 23: // toggle flanger
					if (fx[3]) 
					{
						BASS_ChannelRemoveFX(fxchan,fx[3]);
						fx[3]=0;
					} else
						fx[3]=BASS_ChannelSetFX(fxchan,BASS_FX_DX8_FLANGER,0);
					return 1;
			}
			break;
		case WM_HSCROLL:
			if (l) {
				float level=SendMessage((HWND)l,TBM_GETPOS,0,0)/100.0f; // get level
				//BASS_ASIO_ChannelSetVolume(FALSE,0,level); // set left output level
				//BASS_ASIO_ChannelSetVolume(FALSE,1,level); // set right output level
				BASS_ASIO_ChannelSetVolume(FALSE,global_outputAudioChannelSelectors[0],level); // set left output level
				BASS_ASIO_ChannelSetVolume(FALSE,global_outputAudioChannelSelectors[1],level); // set right output level
			}
			return 1;
		case WM_INITDIALOG:
			win=h;
			MESS(11,TBM_SETRANGE,FALSE,MAKELONG(0,100)); // initialize input level slider
			MessageBox(win,
				"Do not set the input to 'WAVE' / 'What you hear' (etc...) with\n"
				"the level set high, as that is likely to result in nasty feedback.\n",
				"Feedback warning",MB_ICONWARNING);
			if (!Initialize()) {
				DestroyWindow(win);
				return 1;
			}
			return 1;

		case WM_DESTROY:
			// release it all
			BASS_ASIO_Free();
			BASS_Free();
			return 1;
	}
	return 0;
}

PCHAR*
    CommandLineToArgvA(
        PCHAR CmdLine,
        int* _argc
        )
    {
        PCHAR* argv;
        PCHAR  _argv;
        ULONG   len;
        ULONG   argc;
        CHAR   a;
        ULONG   i, j;

        BOOLEAN  in_QM;
        BOOLEAN  in_TEXT;
        BOOLEAN  in_SPACE;

        len = strlen(CmdLine);
        i = ((len+2)/2)*sizeof(PVOID) + sizeof(PVOID);

        argv = (PCHAR*)GlobalAlloc(GMEM_FIXED,
            i + (len+2)*sizeof(CHAR));

        _argv = (PCHAR)(((PUCHAR)argv)+i);

        argc = 0;
        argv[argc] = _argv;
        in_QM = FALSE;
        in_TEXT = FALSE;
        in_SPACE = TRUE;
        i = 0;
        j = 0;

        while( a = CmdLine[i] ) {
            if(in_QM) {
                if(a == '\"') {
                    in_QM = FALSE;
                } else {
                    _argv[j] = a;
                    j++;
                }
            } else {
                switch(a) {
                case '\"':
                    in_QM = TRUE;
                    in_TEXT = TRUE;
                    if(in_SPACE) {
                        argv[argc] = _argv+j;
                        argc++;
                    }
                    in_SPACE = FALSE;
                    break;
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                    if(in_TEXT) {
                        _argv[j] = '\0';
                        j++;
                    }
                    in_TEXT = FALSE;
                    in_SPACE = TRUE;
                    break;
                default:
                    in_TEXT = TRUE;
                    if(in_SPACE) {
                        argv[argc] = _argv+j;
                        argc++;
                    }
                    _argv[j] = a;
                    j++;
                    in_SPACE = FALSE;
                    break;
                }
            }
            i++;
        }
        _argv[j] = '\0';
        argv[argc] = NULL;

        (*_argc) = argc;
        return argv;
    }

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	//LPWSTR *szArgList;
	LPSTR *szArgList;
	int argCount;
	//szArgList = CommandLineToArgvW(GetCommandLineW(), &argCount);
	szArgList = CommandLineToArgvA(GetCommandLine(), &argCount);
	if (szArgList == NULL)
	{
		MessageBox(NULL, "Unable to parse command line", "Error", MB_OK);
		return 10;
	}
	global_audiodevicename="E-MU ASIO"; //"Speakers (2- E-MU E-DSP Audio Processor (WDM))"
	if(argCount>1)
	{
		global_audiodevicename = szArgList[1]; //for spi, device name could be "E-MU ASIO", "Speakers (2- E-MU E-DSP Audio Processor (WDM))", etc.
	}
	global_inputAudioChannelSelectors[0] = 0; 
	global_inputAudioChannelSelectors[1] = 1; 
	if(argCount>2)
	{
		global_inputAudioChannelSelectors[0]=atoi(szArgList[2]); 
	}
	if(argCount>3)
	{
		global_inputAudioChannelSelectors[1]=atoi(szArgList[3]); 
	}
	/*
	global_outputAudioChannelSelectors[0] = 0; // on emu patchmix ASIO device channel 1 (left)
	global_outputAudioChannelSelectors[1] = 1; // on emu patchmix ASIO device channel 2 (right)
	*/
	/*
	global_outputAudioChannelSelectors[0] = 2; // on emu patchmix ASIO device channel 3 (left)
	global_outputAudioChannelSelectors[1] = 3; // on emu patchmix ASIO device channel 4 (right)
	*/
	global_outputAudioChannelSelectors[0] = 4; // on emu patchmix ASIO device channel 15 (left)
	global_outputAudioChannelSelectors[1] = 5; // on emu patchmix ASIO device channel 16 (right)
	if(argCount>4)
	{
		global_outputAudioChannelSelectors[0]=atoi(szArgList[4]); 
	}
	if(argCount>5)
	{
		global_outputAudioChannelSelectors[1]=atoi(szArgList[5]); 
	}
	LocalFree(szArgList);

	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) 
	{
		MessageBox(0,"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}

	BASS_ASIO_DEVICEINFO myBASS_ASIO_DEVICEINFO;
	for (int i=0;BASS_ASIO_GetDeviceInfo(i,&myBASS_ASIO_DEVICEINFO);i++)
	{
		string devicenamestring = myBASS_ASIO_DEVICEINFO.name;
		global_asiodevicemap.insert(pair<string,int>(devicenamestring,i));
	}
	//int deviceid=0;
	map<string,int>::iterator it;
	it = global_asiodevicemap.find(global_audiodevicename);
	if(it!=global_asiodevicemap.end())
	{
		global_deviceid = (*it).second;
		printf("%s maps to %d\n", global_audiodevicename.c_str(), global_deviceid);
	}
	else
	{
		assert(false);
		//Terminate();
	}

	{ // enable trackbar support (for the level control)
		INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_BAR_CLASSES};
		InitCommonControlsEx(&cc);
	}

	DialogBox(hInstance,(char*)1000,0,&dialogproc);

	return 0;
}
