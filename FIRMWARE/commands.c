#include "commands.h"

/*
 * コマンドリスト (元の cmd_table をここに移動)
 * 実際のコマンド処理関数 (cmd_slotReadMem 等) は main.c に残しています。
 * ここは名前と関数ポインタのマッピングのみを行うファイルです。
 */

//// Support Command ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BUFFER Clear                	BCLR	cmd_bufClear        	BCLR(, [Buffer Address],[Length],[Data])          	戻値: OK                      	Buffer RAMのClear
// BUFFER Receive Host         	BRCV	cmd_host2buf        	BRCV,[Buffer Address],[Length]                  	戻値: バイナリーデータ           Buffer RAMからHOSTへのデータ転送
// BUFFER Send Host            	BSND	cmd_buf2Host        	BSND,[Buffer Address],[Length]                  	戻値: OK                    	Buffer RAMへのHOSTからのデータ転送
// HW Version                  	HVER	cmd_hwVersion       	HVER                                            	戻値: Hardware Version + OK   	Hardware Version
// HW information              	HINF	cmd_hwInfomation    	HINF                                            	戻値: Hardware 情報   + OK    	Hardware Information
// HW setting                  	HSET	cmd_hwSetting       	HSET,[Address],[Data]                           	戻値: OK                      	Hardware Setting
// HW Status                   	HSTS	cmd_hwIntenalStatus 	HSTS                                            	戻値: 内部STATUS  + OK        	SlotのCMD実行状態を表示
// Slot Check                  	SCHK	cmd_slotCassetteCheck	SCHK                                            	戻値: SLT1/2の接続状態 +OK    	SlotのCassette接続状態を表示(Power ON不要)
// Slot Power ON               	SPON	cmd_slotPowerOn      	SPON                                            	戻値: OK                      	Slotの電源を入れて、状態のセルフチェック,各信号の有効化,RESETをします。
// Slot Power OFF              	SPOFF	cmd_slotPowerOff    	SPOFF                                           	戻値: OK                      	Slotの電源をOFFにします。
// Slot Mem Read               	SMRD	cmd_slotReadMem     	SMRD,[Address](,[Slot])                         	戻値: Read値（ASCII16進) +OK  	Slotから1Byte Readします
// Slot Mem Write              	SMWR	cmd_slotWriteMem    	SMRD,[Address],[Data](,[Slot])                  	戻値: OK                      	Slotへ1Byte Writeします
// Slot Mem Transfer Read      	SMTR	cmd_slotReadTransfer	SMTR,[Address](,[Length],[Buffer Address],[Slot])	戻値: なし                    	SlotからBufferへ一括Readします
// Slot Mem Transfer Write     	SMTW	cmd_slotWriteTransfer	SMTW,[Address],[Length],[Buffer Address](,[Slot])	戻値: なし                    	BufferからSlotへ一括Writeします
// Slot Select                 	SSEL	cmd_slotSelect      	SSEL,[Slot]                                     	戻値: なし                    	Slot選択
// Slot Reset                  	SRST	cmd_slotReset       	SRST                                            	戻値: OK                      	SlotのReset
// IO Read                     	IORD	cmd_ioRead          	IORD,[IO]                                       	戻値: Read値 + OK             	IOへ1Byte Readします
// IO Write                    	IOWR	cmd_ioWrite         	IOWR,[IO],[Data]                                	戻値: OK                      	IOから1Byte Writeします
// IO Transfer Read            	IOTR	cmd_buf2io          	IOTR,[IO],[Length],[Buffer Address]               	戻値: なし                    	IOへBufferへ一括Readします
// IO Transfer Write           	IOTW	cmd_io2buf          	IOTW,[Buffer Address],[Length],[IO]               	戻値: なし                    	BufferからIOへ一括Writeします
// BUFFER Dump                 	BDMP	cmd_bufDump         	BDMP,[Buffer Address],([Length] )                   戻値: 128Byte DUMP値 +OK      	Bufferのデータ内容の表示(DEBUG用)
// Slot Dump                 	SDMP	cmd_slotDump         	SDMP,[Buffer Address],([Length],[Slot])             戻値: 128Byte DUMP値 +OK      	Bufferのデータ内容の表示(DEBUG用)
// Move2Bootloader Mode    	    _FFU	cmd_ffuMode        	    _FFU            	                                戻値: 無し       	            Bootloaderを起動します。
// Result OFF               	ERROFF	cmd_err_displayOff     	ERROFF                                    	        戻値: 無し                      DISPON　CMD後のOK表示無し
// Result ON               	    ERRON	cmd_err_displayOn     	ERRON                                       	    戻値: ERROFF中の結果            DISPON　CMD後のOK表示の再表示
// LED Color(RDY)               LEDRDY   cmd_ledColorReady      LEDRDY,[R],[G],[B]                                   戻値: 無し                      LEDの色変更(Ready)
// LED Color(PON)               LEDPON   cmd_ledColorPowerOn    LEDPON,[R],[G],[B]                                   戻値: 無し                      LEDの色変更(PowerON)
// LED Color(ACC)               LEDACC   cmd_ledColorAcc        LEDACC,[R],[G],[B]                                   戻値: 無し                      LEDの色変更(Access)

extern int null_format(const Command_t* cmd);           // main.c 実装
extern int cmd_err_displayOff(const Command_t* cmd);    // main.c 実装
extern int cmd_err_displayOn(const Command_t* cmd);     // main.c 実装

extern int cmd_bufClear(const Command_t* cmd);          // main.c 実装
extern int cmd_buf2Host(const Command_t* cmd);          // main.c 実装
extern int cmd_host2buf(const Command_t* cmd);          // main.c 実装
extern int cmd_hwVersion(const Command_t* cmd);         // main.c 実装
extern int cmd_hwInfomation(const Command_t* cmd);      // main.c 実装
extern int cmd_hwIntenalStatus(const Command_t* cmd);   // main.c 実装
extern int cmd_slotCassetteCheck(const Command_t* cmd); // main.c 実装
extern int cmd_slotPowerOn(const Command_t* cmd);       // main.c 実装
extern int cmd_slotPowerOff(const Command_t* cmd);      // main.c 実装
extern int cmd_slotReadMem(const Command_t* cmd);       // main.c 実装
extern int cmd_slotWriteMem(const Command_t* cmd);      // main.c 実装
extern int cmd_slotReadTransfer(const Command_t* cmd);  // main.c 実装
extern int cmd_slotWriteTransfer(const Command_t* cmd); // main.c 実装
extern int cmd_slotSelect(const Command_t* cmd);        // main.c 実装
extern int cmd_slotReset(const Command_t* cmd);         // main.c 実装
extern int cmd_ioRead(const Command_t* cmd);            // main.c 実装
extern int cmd_ioWrite(const Command_t* cmd);           // main.c 実装
extern int cmd_buf2io(const Command_t* cmd);            // main.c 実装
extern int cmd_io2buf(const Command_t* cmd);            // main.c 実装
extern int cmd_ffuMode(const Command_t* cmd);           // main.c 実装
extern int cmd_bufDump(const Command_t* cmd);           // main.c 実装
extern int cmd_slotDump(const Command_t* cmd);          // main.c 実装
extern int cmd_ledColorPowerOn(const Command_t* cmd);   // main.c 実装
extern int cmd_ledColorReady(const Command_t* cmd);     // main.c 実装
extern int cmd_ledColorAcc(const Command_t* cmd);       // main.c 実装
extern int cmd_bufScript(const Command_t* cmd);         // main.c 実装
extern int cmd_loopScript(const Command_t* cmd);         // main.c 実装
extern int cmd_readerFactoryTest(const Command_t* cmd);         // main.c 実装
extern int cmd_setdebuglog(const Command_t* cmd);         // main.c 実装
extern int cmd_slotReadTransferWithHash(const Command_t* cmd);         // main.c 実装
extern int cmd_hset(const Command_t* cmd);         // main.c 実装
extern int cmd_romMapperSet(const Command_t* cmd);         // main.c 実装
extern int cmd_romMapperRead(const Command_t* cmd);         // main.c 実装

const CommandTableEntry cmd_table[] = {
    {"HSET", cmd_hset},		            // HSET,[Address],[Data]		                                Hardware Setting,
    {"ERROFF", cmd_err_displayOff},		// DISPON　CMD後のOK表示無し
    {"ERRON", cmd_err_displayOn},		// DISPON　CMD後のOK表示あり
    {"BCLR", cmd_bufClear},		        // BCLR, [Buffer Address],[Length],[Data]		                Buffer RAMのClear,
    {"BSND", cmd_buf2Host},		        // BSND,[Buffer Address],[Length]		                        Buffer RAMへのHOSTからのデータ転送,
    {"BRCV", cmd_host2buf},		        // BRCV,[Buffer Address],[Length]		                        Buffer RAMからHOSTへのデータ転送,
    {"HVER", cmd_hwVersion},		    // HVER		                                                    Hardware Version,
    {"HINF", cmd_hwInfomation},		    // HINF		                                                    Hardware Information,
    {"HSTS", cmd_hwIntenalStatus},	    // HSTS		                                                    SlotのCMD実行状態を表示,
    {"SCHK", cmd_slotCassetteCheck},	// SCHK		                                                    SlotのCassette接続状態を表示(Power ON不要),
    {"SPON", cmd_slotPowerOn},		    // SPON		                                                    Slotの電源を入れて、状態のセルフチェック,各信号の有効化,RESETをします。,
    {"SPOFF", cmd_slotPowerOff},		// SPOFF		                                                Slotの電源をOFFにします。,
    {"SMRD", cmd_slotReadMem},		    // SMRD,[Address](,[Slot])		                                Slotから1Byte Readします,
    {"SMWR", cmd_slotWriteMem},		    // SMRD,[Address],[Data](,[Slot])		                        Slotへ1Byte Writeします,
    {"SMTR", cmd_slotReadTransfer},		// SMTR,[Address],[Buffer Address],[Length](,[Slot])        	SlotからBufferへ一括Readします, 
    {"SMTW", cmd_slotWriteTransfer},	// SMTW,[Buffer Address],[Address],[Length](,[Slot])        	BufferからSlotへ一括Writeします,
    {"SSEL", cmd_slotSelect},		    // SSEL,[Slot]		                                            Slot選択,
    {"SRST", cmd_slotReset},		    // SRST		                                                    SlotのReset,
    {"IORD", cmd_ioRead},		        // IORD,[IO]		                                            IOへ1Byte Readします,
    {"IOWR", cmd_ioWrite},		        // IOWR,[IO],[Data]		                                        IOから1Byte Writeします,
    {"IOTR", cmd_io2buf},		        // IOTR,[IO],[Buffer Address],[Num](,[Mode])		            IOへBufferへ一括Readします,(mode追加 V1.40～)
    {"IOTW", cmd_buf2io},		        // IOTW,[Buffer Address],[IO],[Num](,[Mode])		            BufferからIOへ一括Writeします,(mode追加 V1.40～)
    {"_FFU", cmd_ffuMode},		        // _FFU
    {"LEDRDY", cmd_ledColorReady},		// LEDRDY,[R],[G],[B]                                           LEDの色変更(Ready)
    {"LEDPON", cmd_ledColorPowerOn},	// LEDPON,[R],[G],[B]                                           LEDの色変更(PowerON)
    {"LEDACC", cmd_ledColorAcc},		// LEDACC,[R],[G],[B]                                           LEDの色変更(Access)
    {"FTEST", cmd_readerFactoryTest},	// Factory Test                                                 製造時のテストを実施します。
    {"BSCR", cmd_bufScript},		    // BSCR,[Buffer Address](,[SLOT])	                            Bufferの上のScriptを実行します。
    {"LSCR", cmd_loopScript},		    // LSCR,[Count]	                                                ScriptをJpLoop回数を設定します。
    {"BDMP", cmd_bufDump},		        // BDMP,[Buffer Address]		                                Bufferのデータ内容の表示(DEBUG用)
    {"SDMP", cmd_slotDump},		        // SDMP,[Buffer Address]		                                Bufferのデータ内容の表示(DEBUG用)
    {"SDBGON", cmd_setdebuglog},	    // SDBGON                   	                                シリアルのデバッグ出力を有効にする
    {"SMTH", cmd_slotReadTransferWithHash},	    // SMTH,[Address],[Buffer Address],[Length](,[Slot])    (追加 V1.20～) 当該エリアをReadしてHash値を計算し返します
    {"RMSET", cmd_romMapperSet},	    // RMSET,[Mapper Selecter Address],[Bank Address],[Bank size]   (追加 V1.40～) Mega ROM Mapperの設定
    {"RMRD", cmd_romMapperRead}	        // RMRD,[Mapper Start],[Mapper End](,[Slot])                    (追加 V1.40～) Mega ROMの一括Read
};

const size_t cmd_table_size = sizeof(cmd_table) / sizeof(CommandTableEntry); // テーブルサイズ計算
