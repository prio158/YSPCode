#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <fstream>

#include "include/FlvParser.h"

using namespace std;

void Process(fstream &fin, const char *filename);

int main(int argc, char *argv[])
{
    cout << "Hi, this is FLV parser test program!\n";

    if (argc != 3)
    {
        cout << "FlvParser.exe [input flv] [output flv]" << endl;
        return 0;
    }

    fstream fin;
    fin.open(argv[1], ios_base::in | ios_base::binary);
    if (!fin)
        return 0;

    Process(fin, argv[2]);

    fin.close();

    return 1;
}

void Process(fstream &fin, const char *filename)
{
    CFlvParser parser;
    //循环读取，每次读取2M
    int nBufSize = 2*1024 * 1024;
    int nFlvPos = 0;
    uint8_t *pBuf, *pBak;
    pBuf = new uint8_t[nBufSize]; //2M字节的数组
    pBak = new uint8_t[nBufSize];

    while (1)
    {
        int nReadNum = 0;
        int nUsedLen = 0;
        fin.read((char *)pBuf + nFlvPos, nBufSize - nFlvPos);
        nReadNum = fin.gcount();
        if (nReadNum == 0)
            break;
        nFlvPos += nReadNum; //下一次读取
        //解析FLV开始，每次解析个2M
        parser.Parse(pBuf, nFlvPos, nUsedLen);
        if (nFlvPos != nUsedLen)
        {
            memcpy(pBak, pBuf + nUsedLen, nFlvPos - nUsedLen);
            memcpy(pBuf, pBak, nFlvPos - nUsedLen);
        }
        nFlvPos -= nUsedLen;
    }
    parser.PrintInfo();
    parser.DumpH264("parser.264");
    parser.DumpAAC("parser.aac");

    //dump into flv
    parser.DumpFlv(filename);

    delete []pBak;
    delete []pBuf;
}
