/*
 * ibm405.h
 *
 *	This was derived from the ppc4xx.h and contains
 *	common 405 offsets
 *
 *      Armin Kuster akuster@pacbell.net
 *      Jan, 2002
 *
 *
 * Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Version 1.0 (02/01/17) - A. Kuster
 *	Initial version	 - moved 405  specific out of the other core.h's
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM405_H__
#define __ASM_IBM405_H__

#ifdef DCRN_BE_BASE
#define DCRN_BEAR	(DCRN_BE_BASE + 0x0)	/* Bus Error Address Register */
#define DCRN_BESR	(DCRN_BE_BASE + 0x1)	/* Bus Error Syndrome Register */
#endif
/* DCRN_BESR */
#define BESR_DSES	0x80000000	/* Data-Side Error Status */
#define BESR_DMES	0x40000000	/* DMA Error Status */
#define BESR_RWS	0x20000000	/* Read/Write Status */
#define BESR_ETMASK	0x1C000000	/* Error Type */
#define ET_PROT	0
#define ET_PARITY	1
#define ET_NCFG	2
#define ET_BUSERR	4
#define ET_BUSTO	6

#ifdef DCRN_CHCR_BASE
#define DCRN_CHCR0	(DCRN_CHCR_BASE + 0x0)	/* Chip Control Register 1 */
#define DCRN_CHCR1	(DCRN_CHCR_BASE + 0x1)	/* Chip Control Register 2 */
#endif
#define CHR1_CETE	0x00800000	/* CPU external timer enable */
#define CHR1_PCIPW	0x00008000	/* PCI Int enable/Peripheral Write enable */

#ifdef DCRN_CHPSR_BASE
#define DCRN_CHPSR	(DCRN_CHPSR_BASE + 0x0)	/* Chip Pin Strapping */
#endif

#ifdef DCRN_CPMFR_BASE
#define DCRN_CPMFR	(DCRN_CPMFR_BASE + 0x0)	/* CPM Force */
#endif

#ifdef DCRN_CPMSR_BASE
#define DCRN_CPMSR	(DCRN_CPMSR_BASE + 0x0)	/* CPM Status */
#define DCRN_CPMER	(DCRN_CPMSR_BASE + 0x1)	/* CPM Enable */
#endif

#ifdef DCRN_DCP0_BASE
/* Decompression Controller Address */
#define DCRN_DCP0_CFGADDR	(DCRN_DCP0_BASE + 0x0)
/* Decompression Controller Data */
#define DCRN_DCP0_CFGDATA	(DCRN_DCP0_BASE + 0x1)
#else
#define DCRN_DCP0_CFGADDR	0x0
#define DCRN_DCP0_CFGDATA	0x0
#endif

#ifdef DCRN_DMA0_BASE
/* DMA Channel Control Register 0 */
#define DCRN_DMACR0	(DCRN_DMA0_BASE + 0x0)
#define DCRN_DMACT0	(DCRN_DMA0_BASE + 0x1)	/* DMA Count Register 0 */
/* DMA Destination Address Register 0 */
#define DCRN_DMADA0	(DCRN_DMA0_BASE + 0x2)
/* DMA Source Address Register 0 */
#define DCRN_DMASA0	(DCRN_DMA0_BASE + 0x3)
#ifdef DCRNCAP_DMA_CC
/* DMA Chained Count Register 0 */
#define DCRN_DMACC0	(DCRN_DMA0_BASE + 0x4)
#endif
#ifdef DCRNCAP_DMA_SG
/* DMA Scatter/Gather Descriptor Addr 0 */
#define DCRN_ASG0	(DCRN_DMA0_BASE + 0x4)
#endif
#endif

#ifdef DCRN_DMA1_BASE
/* DMA Channel Control Register 1 */
#define DCRN_DMACR1	(DCRN_DMA1_BASE + 0x0)
#define DCRN_DMACT1	(DCRN_DMA1_BASE + 0x1)	/* DMA Count Register 1 */
/* DMA Destination Address Register 1 */
#define DCRN_DMADA1	(DCRN_DMA1_BASE + 0x2)
/* DMA Source Address Register 1 */
#define DCRN_DMASA1	(DCRN_DMA1_BASE + 0x3)	/* DMA Source Address Register 1 */
#ifdef DCRNCAP_DMA_CC
/* DMA Chained Count Register 1 */
#define DCRN_DMACC1	(DCRN_DMA1_BASE + 0x4)
#endif
#ifdef DCRNCAP_DMA_SG
/* DMA Scatter/Gather Descriptor Addr 1 */
#define DCRN_ASG1	(DCRN_DMA1_BASE + 0x4)
#endif
#endif

#ifdef DCRN_DMA2_BASE
#define DCRN_DMACR2	(DCRN_DMA2_BASE + 0x0)	/* DMA Channel Control Register 2 */
#define DCRN_DMACT2	(DCRN_DMA2_BASE + 0x1)	/* DMA Count Register 2 */
#define DCRN_DMADA2	(DCRN_DMA2_BASE + 0x2)	/* DMA Destination Address Register 2 */
#define DCRN_DMASA2	(DCRN_DMA2_BASE + 0x3)	/* DMA Source Address Register 2 */
#ifdef DCRNCAP_DMA_CC
#define DCRN_DMACC2	(DCRN_DMA2_BASE + 0x4)	/* DMA Chained Count Register 2 */
#endif
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASG2	(DCRN_DMA2_BASE + 0x4)	/* DMA Scatter/Gather Descriptor Addr 2 */
#endif
#endif

#ifdef DCRN_DMA3_BASE
#define DCRN_DMACR3	(DCRN_DMA3_BASE + 0x0)	/* DMA Channel Control Register 3 */
#define DCRN_DMACT3	(DCRN_DMA3_BASE + 0x1)	/* DMA Count Register 3 */
#define DCRN_DMADA3	(DCRN_DMA3_BASE + 0x2)	/* DMA Destination Address Register 3 */
#define DCRN_DMASA3	(DCRN_DMA3_BASE + 0x3)	/* DMA Source Address Register 3 */
#ifdef DCRNCAP_DMA_CC
#define DCRN_DMACC3	(DCRN_DMA3_BASE + 0x4)	/* DMA Chained Count Register 3 */
#endif
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASG3	(DCRN_DMA3_BASE + 0x4)	/* DMA Scatter/Gather Descriptor Addr 3 */
#endif
#endif

#ifdef DCRN_DMASR_BASE
#define DCRN_DMASR	(DCRN_DMASR_BASE + 0x0)	/* DMA Status Register */
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASGC	(DCRN_DMASR_BASE + 0x3)	/* DMA Scatter/Gather Command */
/* don't know if these two registers always exist if scatter/gather exists */
#define DCRN_POL	(DCRN_DMASR_BASE + 0x6)	/* DMA Polarity Register */
#define DCRN_SLP	(DCRN_DMASR_BASE + 0x5)	/* DMA Sleep Register */
#endif
#endif

#ifdef DCRN_EBC_BASE
#define DCRN_EBCCFGADR	(DCRN_EBC_BASE + 0x0)	/* Peripheral Controller Address */
#define DCRN_EBCCFGDATA	(DCRN_EBC_BASE + 0x1)	/* Peripheral Controller Data */
#endif

#ifdef DCRN_EXIER_BASE
#define DCRN_EXIER	(DCRN_EXIER_BASE + 0x0)	/* External Interrupt Enable Register */
#endif

#ifdef DCRN_EXISR_BASE
#define DCRN_EXISR	(DCRN_EXISR_BASE + 0x0)	/* External Interrupt Status Register */
#endif

#define EXIER_CIE	0x80000000	/* Critical Interrupt Enable */
#define EXIER_SRIE	0x08000000	/* Serial Port Rx Int. Enable */
#define EXIER_STIE	0x04000000	/* Serial Port Tx Int. Enable */
#define EXIER_JRIE	0x02000000	/* JTAG Serial Port Rx Int. Enable */
#define EXIER_JTIE	0x01000000	/* JTAG Serial Port Tx Int. Enable */
#define EXIER_D0IE	0x00800000	/* DMA Channel 0 Interrupt Enable */
#define EXIER_D1IE	0x00400000	/* DMA Channel 1 Interrupt Enable */
#define EXIER_D2IE	0x00200000	/* DMA Channel 2 Interrupt Enable */
#define EXIER_D3IE	0x00100000	/* DMA Channel 3 Interrupt Enable */
#define EXIER_E0IE	0x00000010	/* External Interrupt 0 Enable */
#define EXIER_E1IE	0x00000008	/* External Interrupt 1 Enable */
#define EXIER_E2IE	0x00000004	/* External Interrupt 2 Enable */
#define EXIER_E3IE	0x00000002	/* External Interrupt 3 Enable */
#define EXIER_E4IE	0x00000001	/* External Interrupt 4 Enable */

#ifdef DCRN_IOCR_BASE
#define DCRN_IOCR	(DCRN_IOCR_BASE + 0x0)	/* Input/Output Configuration Register */
#endif
#define IOCR_E0TE	0x80000000
#define IOCR_E0LP	0x40000000
#define IOCR_E1TE	0x20000000
#define IOCR_E1LP	0x10000000
#define IOCR_E2TE	0x08000000
#define IOCR_E2LP	0x04000000
#define IOCR_E3TE	0x02000000
#define IOCR_E3LP	0x01000000
#define IOCR_E4TE	0x00800000
#define IOCR_E4LP	0x00400000
#define IOCR_EDT	0x00080000
#define IOCR_SOR	0x00040000
#define IOCR_EDO	0x00008000
#define IOCR_2XC	0x00004000
#define IOCR_ATC	0x00002000
#define IOCR_SPD	0x00001000
#define IOCR_BEM	0x00000800
#define IOCR_PTD	0x00000400
#define IOCR_ARE	0x00000080
#define IOCR_DRC	0x00000020
#define IOCR_RDM(x)	(((x) & 0x3) << 3)
#define IOCR_TCS	0x00000004
#define IOCR_SCS	0x00000002
#define IOCR_SPC	0x00000001

#ifdef DCRN_MAL_BASE
#define DCRN_MALCR		(DCRN_MAL_BASE + 0x0)	/* MAL Configuration */
#define DCRN_MALDBR		(DCRN_MAL_BASE + 0x3)	/* Debug Register */
#define DCRN_MALESR		(DCRN_MAL_BASE + 0x1)	/* Error Status */
#define DCRN_MALIER		(DCRN_MAL_BASE + 0x2)	/* Interrupt Enable */
#define DCRN_MALTXCARR		(DCRN_MAL_BASE + 0x5)	/* TX Channed Active Reset Register */
#define DCRN_MALTXCASR		(DCRN_MAL_BASE + 0x4)	/* TX Channel Active Set Register */
#define DCRN_MALTXDEIR		(DCRN_MAL_BASE + 0x7)	/* Tx Descriptor Error Interrupt */
#define DCRN_MALTXEOBISR	(DCRN_MAL_BASE + 0x6)	/* Tx End of Buffer Interrupt Status */
#define DCRN_MALRXCARR		(DCRN_MAL_BASE + 0x11)	/* RX Channed Active Reset Register */
#define DCRN_MALRXCASR		(DCRN_MAL_BASE + 0x10)	/* RX Channel Active Set Register */
#define DCRN_MALRXDEIR		(DCRN_MAL_BASE + 0x13)	/* Rx Descriptor Error Interrupt */
#define DCRN_MALRXEOBISR	(DCRN_MAL_BASE + 0x12)	/* Rx End of Buffer Interrupt Status */
#define DCRN_MALRXCTP0R		(DCRN_MAL_BASE + 0x40)	/* Channel Rx 0 Channel Table Pointer */
#define DCRN_MALTXCTP0R		(DCRN_MAL_BASE + 0x20)	/* Channel Tx 0 Channel Table Pointer */
#define DCRN_MALTXCTP1R		(DCRN_MAL_BASE + 0x21)	/* Channel Tx 1 Channel Table Pointer */
#define DCRN_MALRCBS0		(DCRN_MAL_BASE + 0x60)	/* Channel Rx 0 Channel Buffer Size */
#endif
	/* DCRN_MALCR */
#define MALCR_MMSR		0x80000000	/* MAL Software reset */
#define MALCR_PLBP_1		0x00400000	/* MAL reqest priority: */
#define MALCR_PLBP_2		0x00800000	/* lowsest is 00 */
#define MALCR_PLBP_3		0x00C00000	/* highest */
#define MALCR_GA		0x00200000	/* Guarded Active Bit */
#define MALCR_OA		0x00100000	/* Ordered Active Bit */
#define MALCR_PLBLE		0x00080000	/* PLB Lock Error Bit */
#define MALCR_PLBLT_1		0x00040000	/* PLB Latency Timer */
#define MALCR_PLBLT_2 		0x00020000
#define MALCR_PLBLT_3		0x00010000
#define MALCR_PLBLT_4		0x00008000
#define MALCR_PLBLT_DEFAULT	0x00078000	/* JSP: Is this a valid default?? */
#define MALCR_PLBB		0x00004000	/* PLB Burst Deactivation Bit */
#define MALCR_OPBBL		0x00000080	/* OPB Lock Bit */
#define MALCR_EOPIE		0x00000004	/* End Of Packet Interrupt Enable */
#define MALCR_LEA		0x00000002	/* Locked Error Active */
#define MALCR_MSD		0x00000001	/* MAL Scroll Descriptor Bit */
/* DCRN_MALESR */
#define MALESR_EVB		0x80000000	/* Error Valid Bit */
#define MALESR_CIDRX		0x40000000	/* Channel ID Receive */
#define MALESR_DE		0x00100000	/* Descriptor Error */
#define MALESR_OEN		0x00080000	/* OPB Non-Fullword Error */
#define MALESR_OTE		0x00040000	/* OPB Timeout Error */
#define MALESR_OSE		0x00020000	/* OPB Slave Error */
#define MALESR_PEIN		0x00010000	/* PLB Bus Error Indication */
#define MALESR_DEI		0x00000010	/* Descriptor Error Interrupt */
#define MALESR_ONEI		0x00000008	/* OPB Non-Fullword Error Interrupt */
#define MALESR_OTEI		0x00000004	/* OPB Timeout Error Interrupt */
#define MALESR_OSEI		0x00000002	/* OPB Slace Error Interrupt */
#define MALESR_PBEI		0x00000001	/* PLB Bus Error Interrupt */
/* DCRN_MALIER */
#define MALIER_DE		0x00000010	/* Descriptor Error Interrupt Enable */
#define MALIER_NE		0x00000008	/* OPB Non-word Transfer Int Enable */
#define MALIER_TE		0x00000004	/* OPB Time Out Error Interrupt Enable */
#define MALIER_OPBE		0x00000002	/* OPB Slave Error Interrupt Enable */
#define MALIER_PLBE		0x00000001	/* PLB Error Interrupt Enable */
/* DCRN_MALTXEOBISR */
#define MALOBISR_CH0		0x80000000	/* EOB channel 1 bit */
#define MALOBISR_CH2		0x40000000	/* EOB channel 2 bit */

#ifdef DCRN_PLB0_BASE
#define DCRN_PLB0_BESR	(DCRN_PLB0_BASE + 0x0)
#define DCRN_PLB0_BEAR	(DCRN_PLB0_BASE + 0x2)
/* doesn't exist on stb03xxx? */
#define DCRN_PLB0_ACR	(DCRN_PLB0_BASE + 0x3)
#endif

#ifdef DCRN_PLB1_BASE
#define DCRN_PLB1_BESR	(DCRN_PLB1_BASE + 0x0)
#define DCRN_PLB1_BEAR	(DCRN_PLB1_BASE + 0x1)
/* doesn't exist on stb03xxx? */
#define DCRN_PLB1_ACR	(DCRN_PLB1_BASE + 0x2)
#endif

#ifdef DCRN_PLLMR_BASE
#define DCRN_PLLMR	(DCRN_PLLMR_BASE + 0x0)	/* PL1 Mode */
#endif

#ifdef DCRN_POB0_BASE
#define DCRN_POB0_BESR0	(DCRN_POB0_BASE + 0x0)
#define DCRN_POB0_BEAR	(DCRN_POB0_BASE + 0x2)
#define DCRN_POB0_BESR1	(DCRN_POB0_BASE + 0x4)
#endif

#ifdef DCRN_UIC0_BASE
#define DCRN_UIC0_SR	(DCRN_UIC0_BASE + 0x0)
#define DCRN_UIC0_ER	(DCRN_UIC0_BASE + 0x2)
#define DCRN_UIC0_CR	(DCRN_UIC0_BASE + 0x3)
#define DCRN_UIC0_PR	(DCRN_UIC0_BASE + 0x4)
#define DCRN_UIC0_TR	(DCRN_UIC0_BASE + 0x5)
#define DCRN_UIC0_MSR	(DCRN_UIC0_BASE + 0x6)
#define DCRN_UIC0_VR	(DCRN_UIC0_BASE + 0x7)
#define DCRN_UIC0_VCR	(DCRN_UIC0_BASE + 0x8)
#endif

#ifdef DCRN_UIC1_BASE
#define DCRN_UIC1_SR	(DCRN_UIC1_BASE + 0x0)
#define DCRN_UIC1_SRS	(DCRN_UIC1_BASE + 0x1)
#define DCRN_UIC1_ER	(DCRN_UIC1_BASE + 0x2)
#define DCRN_UIC1_CR	(DCRN_UIC1_BASE + 0x3)
#define DCRN_UIC1_PR	(DCRN_UIC1_BASE + 0x4)
#define DCRN_UIC1_TR	(DCRN_UIC1_BASE + 0x5)
#define DCRN_UIC1_MSR	(DCRN_UIC1_BASE + 0x6)
#define DCRN_UIC1_VR	(DCRN_UIC1_BASE + 0x7)
#define DCRN_UIC1_VCR	(DCRN_UIC1_BASE + 0x8)
#endif

#ifdef DCRN_SDRAM0_BASE
#define DCRN_SDRAM0_CFGADDR	(DCRN_SDRAM0_BASE + 0x0)	/* Memory Controller Address */
#define DCRN_SDRAM0_CFGDATA	(DCRN_SDRAM0_BASE + 0x1)	/* Memory Controller Data */
#endif

#ifdef DCRN_OCM0_BASE
#define DCRN_OCMISARC	(DCRN_OCM0_BASE + 0x0)	/* OCM Instr Side Addr Range Compare */
#define DCRN_OCMISCR	(DCRN_OCM0_BASE + 0x1)	/* OCM Instr Side Control */
#define DCRN_OCMDSARC	(DCRN_OCM0_BASE + 0x2)	/* OCM Data Side Addr Range Compare */
#define DCRN_OCMDSCR	(DCRN_OCM0_BASE + 0x3)	/* OCM Data Side Control */
#endif

#endif				/* __ASM_IBM405_H__ */
#endif				/* __KERNEL__ */
