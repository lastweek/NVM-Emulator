/***
 * Support Xeon:
 * O	Platform:		Xeon® E5 v3 and Xeon® E7 v3
 *	Microarchitecture:	Haswell-EP, Haswell-EX
 *
 ***
 * Precious Xeon:
 * O	Platform:		Xeon® E5 v2 and Xeon® E7 v2
 *	Microarchitecture:	Ivy Bridge-EP, Ivy Bridge-EX
 *	MSR Description:
 *	(Intel SDM Volume 3, Chapter 35.8 MSRS IN INTEL® PROCESSOR FAMILY
 *	 BASED ON INTEL® MICROARCHITECTURE CODE NAME IVY BRIDGE)
 *
 * O	Platform:		Xeon® E5 v1
 *	Microarchitecture:	Sandy Bridge-EP
 *	MSR Description:
 *	(Intel SDM Volume 3, Chapter 35.8 MSRS IN INTEL® PROCESSOR FAMILY
 *	 BASED ON INTEL® MICROARCHITECTURE CODE NAME SANDY BRIDGE)
 *
 * O	Platform:		Xeon® E7 v1
 *	Microarchitecture:	Westmere-EX
 *	MSR Description:
 */

/* HSWep Uncore Per-Socket MSRs */
#define HSWEP_MSR_PMON_GLOBAL_CTL		0x700
#define HSWEP_MSR_PMON_GLOBAL_STATUS		0x701
#define HSWEP_MSR_PMON_GLOBAL_CONFIG		0x702

/* HSWep Uncore U-box */
#define HSWEP_MSR_U_PMON_BOX_STATUS		0x708
#define HSWEP_MSR_U_PMON_UCLK_FIXED_CTL		0x703
#define HSWEP_MSR_U_PMON_UCLK_FIXED_CTR		0x704
#define HSWEP_MSR_U_PMON_EVNTSEL0		0x705
#define HSWEP_MSR_U_PMON_CTR0			0x709

/* HSWep Uncore PCU-box */
#define HSWEP_MSR_PCU_PMON_BOX_CTL		0x710
#define HSWEP_MSR_PCU_PMON_BOX_FILTER		0x715
#define HSWEP_MSR_PCU_PMON_BOX_STATUS		0x716
#define HSWEP_MSR_PCU_PMON_EVNTSEL0		0x711
#define HSWEP_MSR_PCU_PMON_CTR0			0x717

/* HSWep Uncore S-box */
#define HSWEP_MSR_S_PMON_BOX_CTL		0x720
#define HSWEP_MSR_S_PMON_BOX_FILTER		0x725
#define HSWEP_MSR_S_PMON_EVNTSEL0		0x721
#define HSWEP_MSR_S_PMON_CTR0			0x726
#define HSWEP_MSR_S_MSR_OFFSET			0xa

/* HSWep Uncore C-box */
#define HSWEP_MSR_C_PMON_BOX_CTL		0xE00
#define HSWEP_MSR_C_PMON_BOX_FILTER0		0xE05
#define HSWEP_MSR_C_PMON_BOX_FILTER1		0xE06
#define HSWEP_MSR_C_PMON_BOX_STATUS		0xE07
#define HSWEP_MSR_C_PMON_EVNTSEL0		0xE01
#define HSWEP_MSR_C_PMON_CTR0			0xE08
#define HSWEP_MSR_C_MSR_OFFSET			0x10


struct uncore_box_ops {
	void (*init_box)(void);
	void (*disable_box)(void);
	void (*enable_event)(void);
};

struct uncore_event_desc {

};

struct uncore_pmu_type {
	const char	*name;
	unsigned int	num_counters;
	unsigned int	num_boxes;
	unsigned int	perf_ctr_bits;
	unsigned int	perf_ctr;
	unsigned int	perf_ctl;
	unsigned int	event_mask;
	unsigned int	fixed_ctr_bits;
	unsigned int	fixed_ctr;
	unsigned int	fixed_ctl;
	unsigned int	box_ctl;
	unsigned int	box_status;
	unsigned int	msr_offset;
	
	struct uncore_box_ops *ops;
	struct uncore_event_desc *desc;
};

struct uncore_pmu_type hswep_ubox = {
	.name		= "U-Box";
	.num_counters	= 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.perf_ctr	= HSWEP_MSR_U_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_U_PMON_EVNTSEL0,
	.event_mask	= 0,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= HSWEP_MSR_U_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= HSWEP_MSR_U_PMON_UCLK_FIXED_CTL,
	.ops		= &hswep_uncore_msr_ops
};

struct uncore_pmu_type hswep_pcubox = {

};

struct uncore_pmu_type hswep_sbox = {

};

struct uncore_pmu_type hswep_cbox = {

};

struct uncore_pmu_type *uncore_msr_pmu[] = {
	&hswep_ubox,
	&hswep_pcubox,
	&hswep_sbox,
	&hswep_cbox
};
