/*
 * Copyright (C) 2015 MediaTek Inc.
 * Copyright (C) 2019 XiaoMi, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>

#include "lens_info.h"

#define AF_I2C_SLAVE_ADDR 0x18
#define AF_DRVNAME "CACTUS_FP5510E2AF_SUNNY_DRV"

#define AF_DEBUG
#ifdef AF_DEBUG
#define LOG_INF(format, args...) pr_debug(AF_DRVNAME " [%s] " format, __func__, ##args)
#else
#define LOG_INF(format, args...)
#endif

static struct i2c_client *g_pstAF_I2Cclient;
static spinlock_t *g_pAF_SpinLock;
static int *g_pAF_Opened;

static unsigned long g_u4AF_INF;
static unsigned long g_u4AF_MACRO = 1023;
static unsigned long g_u4TargetPosition;
static unsigned long g_u4CurrPosition;

static int s4AF_ReadReg(unsigned short *a_pu2Result)
{
	char pBuff[2];

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

	if (i2c_master_recv(g_pstAF_I2Cclient, pBuff, 2) < 0) {
		LOG_INF("I2C read failed!\n");
		return -1;
	}

	*a_pu2Result = (((u16)pBuff[0]) << 4) + (pBuff[1] >> 4);

	return 0;
}

static int s4AF_WriteReg(u16 a_u2Data)
{
	char puSendCmd[2] = {(char)(a_u2Data >> 4), (char)((a_u2Data & 0xF) << 4)};

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

	if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd, 2) < 0) {
		LOG_INF("I2C send failed!\n");
		return -1;
	}

	return 0;
}

static inline int getAFInfo(__user struct stAF_MotorInfo *pstMotorInfo)
{
	struct stAF_MotorInfo stMotorInfo;

	stMotorInfo.u4MacroPosition = g_u4AF_MACRO;
	stMotorInfo.u4InfPosition = g_u4AF_INF;
	stMotorInfo.u4CurrentPosition = g_u4CurrPosition;
	stMotorInfo.bIsSupportSR = 1;
	stMotorInfo.bIsMotorMoving = 1;

	if (*g_pAF_Opened >= 1) {
		stMotorInfo.bIsMotorOpen = 1;
	} else {
		stMotorInfo.bIsMotorOpen = 0;
	}

	if (copy_to_user(pstMotorInfo, &stMotorInfo, sizeof(struct stAF_MotorInfo))) {
		LOG_INF("copy to user failed when getting motor information\n");
	}

	return 0;
}

static inline int moveAF(unsigned long a_u4Position)
{
	int ret = 0;

	if (a_u4Position > g_u4AF_MACRO || a_u4Position < g_u4AF_INF) {
		LOG_INF("out of range\n");
		return -EINVAL;
	}

	if (*g_pAF_Opened == 1) {
		unsigned short InitPos;

		char puSendCmd1[2] = {(char)(0x80), (char)(0x00)};
		char puSendCmd2[2] = {(char)(0x00), (char)(0x00)};
		char puSendCmd3[2] = {(char)(0xEC), (char)(0xA3)};
		char puSendCmd4[2] = {(char)(0xA1), (char)(0x15)};
		char puSendCmd5[2] = {(char)(0xF2), (char)(0x00)};
		char puSendCmd6[2] = {(char)(0xDC), (char)(0x51)};

		g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

		if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd1, 2) < 0){
			LOG_INF("puSendCmd1 send failed\n");
		}

		if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd2, 2) < 0){
			LOG_INF("puSendCmd2 send failed\n");
		}

		if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd3, 2) < 0){
			LOG_INF("puSendCmd3 send failed\n");
		}

		if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd4, 2) < 0){
			LOG_INF("puSendCmd4 send failed\n");
		}

		if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd5, 2) < 0){
			LOG_INF("puSendCmd5 send failed\n");
		}

		if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd6, 2) < 0){
			LOG_INF("puSendCmd6 send failed\n");
		}

		ret = s4AF_ReadReg(&InitPos);
		if (ret == 0) {
			LOG_INF("init Pos: %6d\n", InitPos);

			spin_lock(g_pAF_SpinLock);
			g_u4CurrPosition = (unsigned long)InitPos;
			spin_unlock(g_pAF_SpinLock);
		} else {
			spin_lock(g_pAF_SpinLock);
			g_u4CurrPosition = 0;
			spin_unlock(g_pAF_SpinLock);
		}

		spin_lock(g_pAF_SpinLock);
		*g_pAF_Opened = 2;
		spin_unlock(g_pAF_SpinLock);
	}

	if (g_u4CurrPosition == a_u4Position) {
		return 0;
	}

	spin_lock(g_pAF_SpinLock);
	g_u4TargetPosition = a_u4Position;
	spin_unlock(g_pAF_SpinLock);

	if (s4AF_WriteReg((unsigned short)g_u4TargetPosition) == 0) {
		spin_lock(g_pAF_SpinLock);
		g_u4CurrPosition = (unsigned long)g_u4TargetPosition;
		spin_unlock(g_pAF_SpinLock);
	} else {
		LOG_INF("set I2C failed when moving the motor\n");
		ret = -1;
	}

	return ret;
}

static inline int setAFInf(unsigned long a_u4Position)
{
	spin_lock(g_pAF_SpinLock);
	g_u4AF_INF = a_u4Position;
	spin_unlock(g_pAF_SpinLock);

	return 0;
}

static inline int setAFMacro(unsigned long a_u4Position)
{
	spin_lock(g_pAF_SpinLock);
	g_u4AF_MACRO = a_u4Position;
	spin_unlock(g_pAF_SpinLock);

	return 0;
}

long CACTUS_FP5510E2AF_SUNNY_Ioctl(struct file *a_pstFile, unsigned int a_u4Command, unsigned long a_u4Param)
{
	switch (a_u4Command) {
		case AFIOC_G_MOTORINFO:
			return getAFInfo((__user struct stAF_MotorInfo *)(a_u4Param));
		case AFIOC_T_MOVETO:
			return moveAF(a_u4Param);
		case AFIOC_T_SETINFPOS:
			return setAFInf(a_u4Param);
		case AFIOC_T_SETMACROPOS:
			return setAFMacro(a_u4Param);
		default:
			LOG_INF("no CMD\n");
			return -EPERM;
	}
}

static int s4AF_WriteReg_Directly(u16 a_u2Data)
{
	char puSendCmd[2] = {(char)(a_u2Data >> 8), (char)(a_u2Data & 0xFF)};

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

	if (i2c_master_send(g_pstAF_I2Cclient, puSendCmd, 2) < 0) {
		LOG_INF("I2C send failed!\n");
		return -1;
	}

	return 0;
}

static inline void release_af_smooth(void)
{
	s4AF_WriteReg_Directly(0xECA3);
	s4AF_WriteReg_Directly(0xA111);
	s4AF_WriteReg_Directly(0xF208);
	s4AF_WriteReg_Directly(0xDC51);
	s4AF_WriteReg_Directly(0x12C0);
	mdelay(15);

	s4AF_WriteReg_Directly(0xECA3);
	s4AF_WriteReg_Directly(0xA100);
	s4AF_WriteReg_Directly(0xF200);
	s4AF_WriteReg_Directly(0xDC51);
	s4AF_WriteReg_Directly(0x0006);
	mdelay(100);
}

/* Main jobs:
   1. Deallocate anything that "open" allocated in private_data.
   2. Shut down the device on last close.
   3. Only called once on last time. */
int CACTUS_FP5510E2AF_SUNNY_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	LOG_INF("start\n");

	if (*g_pAF_Opened == 2) {
		LOG_INF("wait\n");
		release_af_smooth();
	}

	if (*g_pAF_Opened) {
		LOG_INF("free\n");

		spin_lock(g_pAF_SpinLock);
		*g_pAF_Opened = 0;
		spin_unlock(g_pAF_SpinLock);
	}

	LOG_INF("end\n");

	return 0;
}

int CACTUS_FP5510E2AF_SUNNY_SetI2Cclient(struct i2c_client *pstAF_I2Cclient, spinlock_t *pAF_SpinLock, int *pAF_Opened)
{
	g_pstAF_I2Cclient = pstAF_I2Cclient;
	g_pAF_SpinLock = pAF_SpinLock;
	g_pAF_Opened = pAF_Opened;

	return 1;
}

int CACTUS_FP5510E2AF_SUNNY_GetFileName(unsigned char *pFileName)
{
	char filePath[256];
	char *fileString;

	sprintf(filePath, "%s", __FILE__);
	fileString = strrchr(filePath, '/');
	*fileString = '\0';

	fileString = (strrchr(filePath, '/') + 1);
	strncpy(pFileName, fileString, AF_MOTOR_NAME);
	LOG_INF("fileName: %s\n", pFileName);

	return 1;
}
