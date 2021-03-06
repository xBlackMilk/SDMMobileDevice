/*
 *  SDMMD_Functions.h
 *  SDMMobileDevice
 *
 *  Copyright (c) 2013, Sam Marshall
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *  3. All advertising materials mentioning features or use of this software must display the following acknowledgement:
 *  	This product includes software developed by the Sam Marshall.
 *  4. Neither the name of the Sam Marshall nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY Sam Marshall ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Sam Marshall BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef _SDM_MD_FUNCTIONS_H_
#define _SDM_MD_FUNCTIONS_H_

#include <CoreFoundation/CoreFoundation.h>
#include <openssl/crypto.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <uuid/uuid.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "SDMMD_MCP.h"
#include "SDMMD_Error.h"
#include "SDMMD_AMDevice.h"
#include "SDMMD_Applications.h"

#if WIN32
#define CFRangeMake(a, b) (CFRange){a, b}
#endif

static kern_return_t sdmmd_mutex_init(pthread_mutex_t thread) {
	kern_return_t result = 0x0;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, 0x2);
	pthread_mutex_init(&thread, &attr);
	result = pthread_mutexattr_destroy(&attr);
	return result;
}

static int SDMMD__mutex_lock(pthread_mutex_t mutex) {
	return pthread_mutex_lock(&mutex);
}

static int SDMMD__mutex_unlock(pthread_mutex_t mutex) {
	return pthread_mutex_unlock(&mutex);
}

static const void* SDMMD___AppendValue(const void* append, void* context) {
	if (CFGetTypeID(append) == CFNumberGetTypeID()) {
		if (CFNumberIsFloatType(append)) {
			float num = 0;
			CFNumberGetValue(append, 0xd, &num);
			append = CFStringCreateWithFormat(kCFAllocatorDefault, 0x0, CFSTR("%g"), num);
		} else {
			uint64_t num = 0;
			CFNumberGetValue(append, 0x4, &num);
			append = CFStringCreateWithFormat(kCFAllocatorDefault, 0x0, CFSTR("%qi"), num);
		}
	} else if (CFGetTypeID(append) == CFBooleanGetTypeID()) {
		append = (CFEqual(append, kCFBooleanTrue) ? CFSTR("1") : CFSTR("0"));
	}
	if (CFGetTypeID(append) == CFStringGetTypeID()) {
		uint32_t length = (uint32_t)CFStringGetLength(append);
		char *alloc = calloc(1, length*8+1);
		CFStringGetBytes(append, CFRangeMake(0x0, length), 0x8000100, 0x0, 0x0, (UInt8*)alloc, (length*8), 0x0);
		CFDataAppendBytes(context, (UInt8*)alloc, (length*8));
		free(alloc);
	}
	return NULL;
}

static void SDMMD___ConvertDictEntry(const void* key, const void* value, void* context) {
	if (key && value) {
		SDMMD___AppendValue(key, context);
		SDMMD___AppendValue(value, context); 
	}
}

static CFMutableDictionaryRef SDMMD__CreateDictFromFileContents(char *path) {
	CFMutableDictionaryRef dict = NULL;
	if (path) {
		struct stat pathStat;
		uint32_t result = lstat(path, &pathStat);
		if (result != 0xff) {
			uint32_t ref = open(path, 0x0);
			if (ref) {
				struct stat fileStat;
				result = fstat(ref, &fileStat);
				if (result != 0xff) {
					unsigned char *data = calloc(1, fileStat.st_size);
					result = (uint32_t)read(ref, data, fileStat.st_size);
					if (result == fileStat.st_size) {
						CFDataRef fileData = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, data, result, kCFAllocatorNull);
						if (fileData) {
							CFTypeRef propList = CFPropertyListCreateWithData(kCFAllocatorDefault, fileData, kCFPropertyListMutableContainersAndLeaves, NULL, NULL);
							if (propList) {
								if (CFGetTypeID(propList) == CFDictionaryGetTypeID()) {
									dict = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0x0, propList);
								} else {
									printf("_CreateDictFromFileAtPath: Plist from file %s was not dictionary type.\n",path);
								}	
							} else {
								printf("_CreateDictFromFileAtPath: Could not create plist from file %s.\n",path);
							}
							CFRelease(fileData);
						}
					} else {
						printf("_CreateDictFromFileAtPath: Could not read contents at file %s.\n",path);
					}
				} else {
					printf("_CreateDictFromFileAtPath: Could not fstat.\n");
				}
			} else {
				printf("_CreateDictFromFileAtPath: Could not open file %s\n",path);
			}
		} else {
			printf("_CreateDictFromFileAtPath: Could not lstat.\n");
		}
	}
	return dict;
}

static CFMutableDictionaryRef SDMMD__CreateMessageDict(CFStringRef type) {
	CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0x0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (dict) {
		CFDictionarySetValue(dict, CFSTR("Request"), type);
		CFDictionarySetValue(dict, CFSTR("ProtocolVersion"), CFSTR("2"));
		char *appName = (char *)getprogname();
		if (appName) {
			CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, appName, 0x8000100);
			if (name) {
				CFDictionarySetValue(dict, CFSTR("Label"), name);
				CFRelease(name);
			}
		}
	}
	return dict;
	
}

static CFMutableDictionaryRef SDMMD_create_dict() {
	return CFDictionaryCreateMutable(NULL, 0x0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

/*static void SDMMD_openSSLLockCallBack(int mode, int n, const char * file, int line) {
	if (mode & CRYPTO_LOCK)
		SDMMD__mutex_lock(SDMMobileDevice->sslLocks[n]);
	else
		SDMMD__mutex_unlock(SDMMobileDevice->sslLocks[n]);
}*/

static unsigned long SDMMD_openSSLThreadIDCallBack() {
	return (unsigned long)pthread_self();
}

static uint32_t SDMMD_lockssl_init() {
	return SSL_get_ex_new_index(0x0, "peer certificate data", 0x0, 0x0, 0x0);
}

static char* SDMMD_ssl_strerror(SSL *ssl, uint32_t length) {
	char *code = "SSL_ERROR_NONE";
	uint32_t result = SSL_get_error(ssl, length);
	if (result < 0x8) {
		switch (result) {
			case 1: {
				result = (uint32_t)ERR_peek_error();
				if (result) {
					code = "SSL_ERROR_SSL";
				} else {
					code = "SSL_ERROR_SSL unknown error";
				}
				break;
			};
			case 2: {
				code = "SSL_ERROR_WANT_READ";
				break;
			};
			case 3: {
				code = "SSL_ERROR_WANT_WRITE";
				break;
			};
			case 4: {
				code = "SSL_ERROR_WANT_X509_LOOKUP";
				break;
			};
			case 5: {
				result = (uint32_t)ERR_peek_error();
				if (result == 0) {
					if (length == 0) {
						code = "SSL_ERROR_SYSCALL (Early EOF reached)";	
					} else {
						code = "SSL_ERROR_SYSCALL errno";
					}
				} else if (result != 0) {
					code = "SSL_ERROR_SYSCALL internal";
				} else {
					code = "SSL_ERROR_SYSCALL (WTFERROR)";
				}
				break;
			};
			case 6: {
				code = "SSL_ERROR_ZERO_RETURN";
				break;
			};
			case 7: {
				code = "SSL_ERROR_WANT_CONNECT";
				break;
			};
			case 8: {
				code = "SSL_ERROR_WANT_ACCEPT";
				break;
			};
			default: {
				code = "Unknown SLL error";
				break;
			};
		}
	} else {
		ERR_print_errors_fp(stderr);
		code = "Unknown SLL error type";
	}
	ERR_clear_error();
	return code;
}

static CFStringRef SDMGetCurrentDateString() {
	CFLocaleRef currentLocale = CFLocaleCopyCurrent();
	CFDateRef date = CFDateCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent());
	CFDateFormatterRef customDateFormatter = CFDateFormatterCreate(NULL, currentLocale, kCFDateFormatterNoStyle, kCFDateFormatterNoStyle);
	CFStringRef customDateFormat = CFSTR("yyyy-MM-dd*HH:mm:ss");
	CFDateFormatterSetFormat(customDateFormatter, customDateFormat);
	CFStringRef customFormattedDateString = CFDateFormatterCreateStringWithDate(NULL, customDateFormatter, date);
	CFRelease(currentLocale);
	CFRelease(date);
	CFRelease(customDateFormatter);
	return customFormattedDateString;
}

static char* SDMCFStringGetString(CFStringRef str) {
	char *cstr = calloc(1, CFStringGetLength(str)+1);
	CFStringGetCString(str, cstr, CFStringGetLength(str), CFStringGetFastestEncoding(str));
	return cstr;
}

static char* SDMCFURLGetString(CFURLRef url) {
	return SDMCFStringGetString(CFURLGetString(url));
}

static void SDMMD__PairingRecordPathForIdentifier(CFStringRef udid, char *path) {
	if (!path)
		path = calloc(0x1, 0x1000);
	char *udidCSTR = calloc(0x1, 0x400);
	char *recordPath = calloc(0x1, 0x400);
	CFStringGetCString(CFSTR("/var/db/lockdown"), recordPath, 0x400, 0x8000100);
	CFStringGetCString(udid, udidCSTR, 0x400, 0x8000100);
	strcat(path, recordPath);
	strcat(path, "/");
	strcat(path, udidCSTR);
	strcat(path, ".plist");
	free(udidCSTR);
	free(recordPath);
}

static CFTypeRef SDMMD_CreateUUID() {
	uuid_t uu;
	char uuid[16];
	uuid_generate(uu);
	uuid_unparse(uu, uuid);
	return CFStringCreateWithCString(NULL, uuid, 0x8000100);
}

static sdmmd_return_t SDMMD_store_dict(CFDictionaryRef dict, char *path, uint32_t mode) {
	sdmmd_return_t result = 0x0;
	char *tmp = calloc(1, strlen(path)+5);
	strcpy(tmp, path);
	strcat(tmp, ".tmp");
	unlink(tmp);
	uint32_t ref = open(tmp, 0xa01);
	if (ref) {
		CFDataRef xml = CFPropertyListCreateXMLData(kCFAllocatorDefault, dict);
		if (xml) {
			result = (sdmmd_return_t)write(ref, CFDataGetBytePtr(xml), CFDataGetLength(xml));
			result = rename(tmp, path);
		}
		close(ref);
		result = chmod(path, 0x1b6);
	}
	return result;
}

static CFTypeRef SDMMD_AMDCopySystemBonjourUniqueID() {
	char *record = calloc(0x1, 0x401);
	CFTypeRef value;
	SDMMD__PairingRecordPathForIdentifier(CFSTR("SystemConfiguration"), record);
	CFMutableDictionaryRef dict = SDMMD__CreateDictFromFileContents(record);
	if (dict == 0) {
		dict = SDMMD_create_dict();
	}
	if (dict) {
		value = CFDictionaryGetValue(dict, CFSTR("SystemBUID"));
		if (value == 0) {
			value = SDMMD_CreateUUID();
			CFDictionarySetValue(dict, CFSTR("SystemBUID"), value);
			SDMMD_store_dict(dict, record, 0x1);
		}
		if (value == 0) {
			printf("SDMMD_AMDCopySystemBonjourUniqueID: Could not generate UUID!\n");
		}
	}
	return value;
}

static sdmmd_return_t SDMMD__CreatePairingRecordFromRecordOnDiskForIdentifier(SDMMD_AMDeviceRef device, CFMutableDictionaryRef *dict) {
	sdmmd_return_t result = 0xe8000007;
	if (device) {
		if (dict) {
			result = 0xe8000003;
			CFTypeRef bonjourId = SDMMD_AMDCopySystemBonjourUniqueID();
			if (bonjourId) {
				char *path = calloc(1, sizeof(char)*0x401);
				SDMMD__PairingRecordPathForIdentifier(device->ivars.unique_device_id, path);
				CFMutableDictionaryRef fileDict = SDMMD__CreateDictFromFileContents(path);
				result = 0xe8000025;
				if (fileDict) {
					CFTypeRef systemId = CFDictionaryGetValue(fileDict, CFSTR("SystemBUID"));
					if (systemId) {
						if (CFGetTypeID(systemId) == CFStringGetTypeID()) {
							CFDictionarySetValue(fileDict, CFSTR("SystemBUID"), bonjourId);
							result = SDMMD_store_dict(fileDict, path, 1);
							if (result) {
								printf("SDMMD__CreatePairingRecordFromRecordOnDiskForIdentifier: Could not store pairing record at '%s'.\n",path);
								result = 0xe800000a;
							} else {
								CFRetain(fileDict);
								*dict = fileDict;
							}
						}
					}
					CFRelease(fileDict);
				}
				free(path);
				CFRelease(bonjourId);
			}
		}
	}
	return result;
}

static CFArrayRef SDMMD_ApplicationLookupDictionary() {
	const void* values[6] = {CFSTR(kAppLookupKeyCFBundleIdentifier), CFSTR(kAppLookupKeyApplicationType), CFSTR(kAppLookupKeyCFBundleDisplayName), CFSTR(kAppLookupKeyCFBundleName), CFSTR(kAppLookupKeyContainer), CFSTR(kAppLookupKeyPath)};
	return CFArrayCreate(kCFAllocatorDefault, values, 6, &kCFTypeArrayCallBacks);
}

static CFURLRef SDMMD__AMDCFURLCreateFromFileSystemPathWithSmarts(CFStringRef path) {
	char *cpath = calloc(1, 0x401);
	CFURLRef url = NULL;
	if (CFStringGetCString(path, cpath, 0x400, 0x8000100)) {
		struct stat buf;
		lstat(cpath, &buf);
		CFURLRef base = CFURLCreateWithString(kCFAllocatorDefault, CFSTR("file://localhost/"), NULL);
		url = CFURLCreateWithFileSystemPathRelativeToBase(kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, S_ISDIR(buf.st_mode), base);
	}
	return url;
}

static CFURLRef SDMMD__AMDCFURLCreateWithFileSystemPathRelativeToBase(CFAllocatorRef allocator, CFStringRef path, CFURLPathStyle style, Boolean dir) {
	CFURLRef base = CFURLCreateWithString(allocator, CFSTR("file://localhost/"), NULL);
	CFURLRef url = CFURLCreateWithFileSystemPathRelativeToBase(allocator, path, style, dir & 0xff, base);
	return url;
}

static void SDMMD__AMDCFURLGetCStringForFileSystemPath(CFURLRef urlRef, char *cpath) {
	cpath = calloc(1, 0x401);
	CFTypeRef url = CFURLCopyFileSystemPath(urlRef, 0x0);
	if (url) {
		CFStringGetCString(url, cpath, 0x401, 0x8000100);
	}
}

#endif