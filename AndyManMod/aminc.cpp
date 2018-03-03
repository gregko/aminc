/*
 * Copyright (C) 2013 Greg Kochaniak, http://www.hyperionics.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Based on APKExtractor by Prasanta Paul, http://prasanta-paul.blogspot.com
 */

#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string>
#include <vector>

using namespace std;

typedef enum ResourceTypes {
    RES_NULL_TYPE = 0x0000,
	RES_STRING_POOL_TYPE = 0x0001,
	RES_TABLE_TYPE = 0x0002,
	RES_XML_TYPE = 0x0003,
					  
	// Chunk types in RES_XML_TYPE
	RES_XML_FIRST_CHUNK_TYPE = 0x0100,
	RES_XML_START_NAMESPACE_TYPE = 0x0100,
	RES_XML_END_NAMESPACE_TYPE = 0x0101,
	RES_XML_START_ELEMENT_TYPE = 0x0102,
	RES_XML_END_ELEMENT_TYPE = 0x0103,
	RES_XML_CDATA_TYPE = 0x0104,
	RES_XML_LAST_CHUNK_TYPE = 0x017f,
					  
	// This contains a uint32_t array mapping strings in the string
	// pool back to resource identifiers. It is optional.
	RES_XML_RESOURCE_MAP_TYPE = 0x0180,
			
	// Chunk types in RES_TABLE_TYPE
	RES_TABLE_PACKAGE_TYPE = 0x0200, 
	RES_TABLE_TYPE_TYPE = 0x0201,
    RES_TABLE_TYPE_SPEC_TYPE = 0x0202}
;

typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned char byte;

#pragma pack(2) // make sure ChunkHeader structure is correctly aligned

class ChunkHeader {
public:
    ushort  type;
    ushort  header_size;
    uint    chunk_size;
};

class StringPoolHeader {
public:
    uint    string_count;
    uint    style_count;
    uint    flag;
    uint    string_start;
    uint    style_start;
};

vector<string> stringPool;
vector<uint> resMap;
int nodeIndex = -1;
int ns_prefix_index = -1;
int ns_uri_index = -1;
int ns_linenumber = 0;

static uint findVersionCode(FILE *fp);

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "Compiled AndroidManifest.xml versionCode modifier version 0.1\n"
            "Written by G. Kochaniak, http://www.hyperionics.com\n"
            "Based on APKExtractor by Prasanta Paul, http://prasanta-paul.blogspot.com\n\n"
            "Usage: aminc file [increment]\n"
            "   file - file name of compiled (binary) AndroidManifest.xml\n"
            "   increment - integer value to increment versionCode by\n\n");
        return 1;
    }
    const char *fname = argv[1];
    FILE *fp = fopen(fname, "r+b");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file: %s\n", fname);
        return 1;
    }
    uint vcode = findVersionCode(fp);
    long fpos = ftell(fp);
    if (vcode != 0) {
        printf("Found in %s:\nversionCode %u (0x%X) at offset %ld (0x%X)\n", fname, vcode, vcode, fpos, fpos);
        if (argc > 2) {
            int increment = atoi(argv[2]);
            if (increment != 0) {
                printf("Incrementing versionCode by %d...\n", increment);
                vcode += increment;
                fseek(fp, fpos, SEEK_SET);
                fwrite(&vcode, sizeof(uint), 1, fp);
                rewind(fp);
                uint vcode2 = findVersionCode(fp);
                if (vcode2 != vcode) { // something went wrong...
                    printf("Error, the new value read from the manifest file is  %u (0x%X)\n", vcode2, vcode2);
                } else {
                    printf("Success, the new value read from the manifest file is  %u (0x%X)\n", vcode2, vcode2);
                }
            }
        }
    }
    fclose(fp);
}

static uint findVersionCodeInternal(FILE *fp, ChunkHeader &chunk);

static uint findVersionCode(FILE *fp)
{
    ChunkHeader chunk;

    fread(&chunk, sizeof(ChunkHeader), 1, fp);
    if (chunk.type != RES_XML_TYPE) {
        fprintf(stderr, "Invalid resource type (%d), must be RES_XML_TYPE (3) for AndroidManifest.xml", chunk.type);
        return 1;
    }

    fread(&chunk, sizeof(ChunkHeader), 1, fp);

    if (chunk.type == RES_STRING_POOL_TYPE) {
        long fpos = ftell(fp) + chunk.chunk_size - sizeof(ChunkHeader); // need to rewind to here after reading stringPool 
        uint bufSize = chunk.chunk_size - sizeof(ChunkHeader);

        StringPoolHeader sph;
        fread(&sph, sizeof(StringPoolHeader), 1, fp);
        
        // Read index location of each string
        vector<uint> string_indices(sph.string_count);
        fread(&string_indices[0], sizeof(uint), sph.string_count, fp);

        // Skip styles
        if (sph.style_count > 0)
            fseek(fp, sph.style_count*sizeof(uint), SEEK_CUR);

        // Read strings
        stringPool.reserve(sph.string_count);
        for (uint i = 0; i < sph.string_count; i++) {
            int string_len = 0;
            if (i == sph.string_count - 1) {
                if(sph.style_start == 0)// There is no Style span
                {
                    // Length of the last string. Chunk Size - Start position of this String - Header - Len of Indices
                    string_len = chunk.chunk_size - string_indices[i] - chunk.header_size - 4 * sph.string_count;
                }
                else
                    string_len = sph.style_start - string_indices[i];
            }
            else
                string_len = string_indices[i+1] - string_indices[i];

			string str_buf;
			if (string_len > 0) {
				/*
				 * Each String entry contains Length header (2 bytes to 4 bytes) + Actual String + [0x00]
				 * Length header sometime contain duplicate values e.g. 20 20
				 * Actual string sometime contains 00, which need to be ignored
				 * Ending zero might be  2 byte or 4 byte
				 *
				 * TODO: Consider both Length bytes and String length > 32767 characters
				 */
				byte short_buf[2];
				fread(&short_buf, sizeof(short_buf), 1, fp);
				int actual_str_len = 0;
				if (short_buf[0] == short_buf[1]) // Its repeating, happens for Non-Manifest file. e.g. 20 20
					actual_str_len = short_buf[0];
				else
					actual_str_len = short_buf[0] + 256 * short_buf[1];

				vector<byte> buf(string_len - 2); // Skip 2 Length bytes, already read.
				fread(&buf[0], buf.size(), 1, fp);
				int j = 0;
				for (uint k = 0; k < buf.size(); k++) {
					// Skipp 0x00
					if (buf[k] != 0x00) {
						str_buf += buf[k];
						if (++j >= actual_str_len)
							break;
					}
				}
			}

            stringPool.push_back(str_buf);
        }

        // Read the next chunk
        fseek(fp, fpos, SEEK_SET);
        fread(&chunk, sizeof(ChunkHeader), 1, fp);
    }

    // Resource Mapping- Optional Content
    if (chunk.type == RES_XML_RESOURCE_MAP_TYPE) {
        long fpos = ftell(fp) + chunk.chunk_size - sizeof(ChunkHeader); // need to rewind to here after reading stringPool 

        uint num_of_res_ids = (chunk.chunk_size - sizeof(ChunkHeader))/4;
        resMap.reserve(num_of_res_ids);
		for(uint i=0; i<num_of_res_ids; i++){
			uint n;
            fread(&n, sizeof(uint), 1, fp);
            resMap.push_back(n);
		}
        // Read the next chunk
        fseek(fp, fpos, SEEK_SET);
        fread(&chunk, sizeof(ChunkHeader), 1, fp);
    }

	/*
		* There can be multiple Name space and XML node sections
		* [XML_NameSpace_Start]
		* 	[XML_Start]
		*  	[XML_Start]
		* 		[XML_End]
		*  [XML_END]
		* [XML_NameSpace_End]
		* [XML_NameSpace_Start]
		* 	[XML_Start]
		* 	[XML_End]
		* [XML_NameSpace_End]
		*/
		
	// Name space Start
	if(chunk.type == RES_XML_START_NAMESPACE_TYPE)
	{
        long fpos = ftell(fp) + chunk.chunk_size - sizeof(ChunkHeader); // need to rewind to here after reading stringPool
		// Parse Start of Name space
		nodeIndex = 0;
		
        fread(&ns_linenumber, sizeof(int), 1, fp);
		int comment;
        fread(&comment, sizeof(int), 1, fp);
        fread(&ns_prefix_index, sizeof(int), 1, fp);
        fread(&ns_uri_index, sizeof(int), 1, fp);

        // Read the next chunk
        fseek(fp, fpos, SEEK_SET);
        fread(&chunk, sizeof(ChunkHeader), 1, fp);
    }
		
	// Handle multiple XML Elements
	while(chunk.type !=  RES_XML_END_NAMESPACE_TYPE)
	{
		/*
			* XML_Start
			* 	XML_Start
			*  XML_End
			* XML_End
			* .......
			*/
		if(chunk.type == RES_XML_START_ELEMENT_TYPE)
		{
			// Start of XML Node
            uint vcode = findVersionCodeInternal(fp, chunk); // (elementBuf, header_size, chunk_size);
            if (vcode != 0) {
                return vcode;
            }
		}
		else // if(chunk.type == RES_XML_END_ELEMENT_TYPE)
		{
			// End of XML Node - just skip for now
            fseek(fp, chunk.chunk_size - sizeof(ChunkHeader), SEEK_CUR);
			//parseXMLEnd(elementBuf, header_size, chunk_size);
            
		}
			
		// TODO: CDATA
			
		// Next Chunk type
		fread(&chunk, sizeof(ChunkHeader), 1, fp);
	}
    
    return 0;
}


static uint findVersionCodeInternal(FILE *fp, ChunkHeader &chunk)
{
	nodeIndex++;
	//Node node = new Node();
	//node.setIndex(nodeIndex);
	
    int lineNumber;
    fread(&lineNumber, sizeof(int), 1, fp);
	// node.setLinenumber(lineNumber);
		
	int comment;
	fread(&comment, sizeof(int), 1, fp);

	int ns_index;
	fread(&ns_index, sizeof(int), 1, fp);
		
	int name_index;
	fread(&name_index, sizeof(int), 1, fp);

	short attributeStart;
	fread(&attributeStart, sizeof(short), 1, fp);
		
	short attributeSize;
    fread(&attributeSize, sizeof(short), 1, fp);
		
	short attributeCount;
    fread(&attributeCount, sizeof(short), 1, fp);
		
	// Skip ID, Class and Style index
    fseek(fp, 6, SEEK_CUR);
		
	// Log.d(tag, "[XML Node] Name: "+ (name_index == -1 || name_index >= stringPool.size() ? "-1" : stringPool.get(name_index)) +" Attr count: "+ attributeCount);
		
	for(int i=0; i<attributeCount; i++)
	{
		//Attribute attr;
			
		// attr ns
		int attr_ns_index;
        fread(&attr_ns_index, sizeof(int), 1, fp);
			
		// attr name
		int attr_name_index;
        fread(&attr_name_index, sizeof(int), 1, fp);
			
		// Raw value. If user has directly mentioned value e.g. android:value="1". Reference to String Pool
		int attr_raw_value; // =  Utils.toInt(int_buf, false);
        fread(&attr_raw_value, sizeof(int), 1, fp);
			
		//string attr_value = "";
			
		if(attr_raw_value == -1){
			// No Raw value defined.
			// Read Typed Value. Reference to Resource table e.g. String.xml, Drawable etc.
			/*
				* Size of Types value- init16
				* Res- init8 (Always 0)
				* Data Type- init8
				* Data- init32. Interpreted according to dataType
				*/
			short data_size; // = Utils.toInt(short_buf, false);
            fread(&data_size, sizeof(short), 1, fp);
				
			// Skip res value- Always 0
			fseek(fp, 1, SEEK_CUR);
				
			// TODO: Retrieve data based on Data_Type. Read Resource table.
            byte data_type;
            fread(&data_type, sizeof(byte), 1, fp);
				
			uint data; // = Utils.toInt(int_buf, false); // Refer to Resource Table
            // if attribute name is "versionCode", then we've found what we were looking for.
            fread(&data, sizeof(uint), 1, fp);
            if (attr_name_index > -1 && attr_name_index < (int) stringPool.size() && stringPool[attr_name_index] == "versionCode") {
                fseek(fp, -((int)sizeof(int)), SEEK_CUR);
                long fpos = ftell(fp) - sizeof(int);
                return data;
            }
            //char bbb[20];
			//attr_value = itoa(data, bbb, 10);
		}
		else{
			//attr_value = attr_raw_value < (int) stringPool.size() ? stringPool[attr_raw_value] : NULL;
			// Skip Typed value bytes
			fseek(fp, 8, SEEK_CUR);
		}
			
		//if(attr_name_index > -1 && attr_name_index < (int) stringPool.size())
		//{
		//	attr.setName( stringPool[attr_name_index]);
		//	attr.setValue(attr_value);
		//	attr.setIndex(i);
        //  printf("%d   \"%s\" \"%s\"   %d\n", attr_name_index, attr.getName().c_str(), attr.getValue().c_str(), i);
		//}
	}
    return 0; // not found
}

