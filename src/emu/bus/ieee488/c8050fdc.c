// license:BSD-3-Clause
// copyright-holders:Curt Coder
/**********************************************************************

    Commodore 8050 floppy disk controller emulation

    Copyright MESS Team.
    Visit http://mamedev.org for licensing and usage restrictions.

**********************************************************************/

#include "c8050fdc.h"



//**************************************************************************
//  MACROS / CONSTANTS
//**************************************************************************

#define LOG 0



//**************************************************************************
//  DEVICE DEFINITIONS
//**************************************************************************

const device_type C8050_FDC = &device_creator<c8050_fdc_t>;


//-------------------------------------------------
//  ROM( c8050_fdc )
//-------------------------------------------------

ROM_START( c8050_fdc )
	ROM_REGION( 0x800, "gcr", 0)
	ROM_LOAD( "901467.uk6", 0x000, 0x800, CRC(a23337eb) SHA1(97df576397608455616331f8e837cb3404363fa2) )
ROM_END


//-------------------------------------------------
//  rom_region - device-specific ROM region
//-------------------------------------------------

const rom_entry *c8050_fdc_t::device_rom_region() const
{
	return ROM_NAME( c8050_fdc );
}



//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

//-------------------------------------------------
//  c8050_fdc_t - constructor
//-------------------------------------------------

c8050_fdc_t::c8050_fdc_t(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
	device_t(mconfig, C8050_FDC, "C8050 FDC", tag, owner, clock, "c8050fdc", __FILE__),
	m_write_sync(*this),
	m_write_ready(*this),
	m_write_error(*this),
	m_gcr_rom(*this, "gcr"),
	m_floppy0(NULL),
	m_floppy1(NULL),
	m_mtr0(1),
	m_mtr1(1),
	m_stp0(0),
	m_stp1(0),
	m_ds(0),
	m_drv_sel(0),
	m_mode_sel(0),
	m_rw_sel(0),
	m_period(attotime::from_hz(clock))
{
	cur_live.tm = attotime::never;
	cur_live.state = IDLE;
	cur_live.next_state = -1;
	cur_live.drv_sel = m_drv_sel;
}



//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void c8050_fdc_t::device_start()
{
	// resolve callbacks
	m_write_sync.resolve_safe();
	m_write_ready.resolve_safe();
	m_write_error.resolve_safe();

	// allocate timer
	t_gen = timer_alloc(0);

	// register for state saving
	save_item(NAME(m_mtr0));
	save_item(NAME(m_mtr1));
	save_item(NAME(m_stp0));
	save_item(NAME(m_stp1));
	save_item(NAME(m_ds));
	save_item(NAME(m_drv_sel));
	save_item(NAME(m_mode_sel));
	save_item(NAME(m_rw_sel));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void c8050_fdc_t::device_reset()
{
	live_abort();
}


//-------------------------------------------------
//  device_timer - handler timer events
//-------------------------------------------------

void c8050_fdc_t::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	live_sync();
	live_run();
}

floppy_image_device* c8050_fdc_t::get_floppy()
{
	return cur_live.drv_sel ? m_floppy1 : m_floppy0;
}

void c8050_fdc_t::stp_w(floppy_image_device *floppy, int mtr, int &old_stp, int stp)
{
	if (mtr) return;

	int tracks = 0;

	switch (old_stp)
	{
	case 0: if (stp == 1) tracks++; else if (stp == 2) tracks--; break;
	case 1: if (stp == 3) tracks++; else if (stp == 0) tracks--; break;
	case 2: if (stp == 0) tracks++; else if (stp == 3) tracks--; break;
	case 3: if (stp == 2) tracks++; else if (stp == 1) tracks--; break;
	}

	if (tracks == -1)
	{
		floppy->dir_w(1);
		floppy->stp_w(1);
		floppy->stp_w(0);
	}
	else if (tracks == 1)
	{
		floppy->dir_w(0);
		floppy->stp_w(1);
		floppy->stp_w(0);
	}

	old_stp = stp;
}

void c8050_fdc_t::stp0_w(int stp)
{
	if (m_stp0 != stp)
	{
		live_sync();
		this->stp_w(m_floppy0, m_mtr0, m_stp0, stp);
		checkpoint();
		live_run();
	}
}

void c8050_fdc_t::stp1_w(int stp)
{
	if (m_stp1 != stp)
	{
		live_sync();
		if (m_floppy1) this->stp_w(m_floppy1, m_mtr1, m_stp1, stp);
		checkpoint();
		live_run();
	}
}

void c8050_fdc_t::ds_w(int ds)
{
	if (m_ds != ds)
	{
		live_sync();
		m_ds = cur_live.ds = ds;
		checkpoint();
		live_run();
	}
}

void c8050_fdc_t::set_floppy(floppy_image_device *floppy0, floppy_image_device *floppy1)
{
	m_floppy0 = floppy0;
	m_floppy1 = floppy1;
}

void c8050_fdc_t::live_start()
{
	cur_live.tm = machine().time();
	cur_live.state = RUNNING;
	cur_live.next_state = -1;

	cur_live.shift_reg = 0;
	cur_live.shift_reg_write = 0;
	cur_live.cycle_counter = 0;
	cur_live.cell_counter = 0;
	cur_live.bit_counter = 0;
	cur_live.ds = m_ds;
	cur_live.drv_sel = m_drv_sel;
	cur_live.mode_sel = m_mode_sel;
	cur_live.rw_sel = m_rw_sel;
	cur_live.pi = m_pi;

	pll_reset(cur_live.tm, attotime::from_double(0));
	checkpoint_live = cur_live;
	pll_save_checkpoint();

	live_run();
}

void c8050_fdc_t::pll_reset(const attotime &when, const attotime clock)
{
	cur_pll.reset(when);
	cur_pll.set_clock(clock);
}

void c8050_fdc_t::pll_start_writing(const attotime &tm)
{
	cur_pll.start_writing(tm);
}

void c8050_fdc_t::pll_commit(floppy_image_device *floppy, const attotime &tm)
{
	cur_pll.commit(floppy, tm);
}

void c8050_fdc_t::pll_stop_writing(floppy_image_device *floppy, const attotime &tm)
{
	cur_pll.stop_writing(floppy, tm);
}

void c8050_fdc_t::pll_save_checkpoint()
{
	checkpoint_pll = cur_pll;
}

void c8050_fdc_t::pll_retrieve_checkpoint()
{
	cur_pll = checkpoint_pll;
}

int c8050_fdc_t::pll_get_next_bit(attotime &tm, floppy_image_device *floppy, const attotime &limit)
{
	return cur_pll.get_next_bit(tm, floppy, limit);
}

bool c8050_fdc_t::pll_write_next_bit(bool bit, attotime &tm, floppy_image_device *floppy, const attotime &limit)
{
	return cur_pll.write_next_bit_prev_cell(bit, tm, floppy, limit);
}

void c8050_fdc_t::checkpoint()
{
	pll_commit(get_floppy(), cur_live.tm);
	checkpoint_live = cur_live;
	pll_save_checkpoint();
}

void c8050_fdc_t::rollback()
{
	cur_live = checkpoint_live;
	pll_retrieve_checkpoint();
}

void c8050_fdc_t::live_sync()
{
	if(!cur_live.tm.is_never()) {
		if(cur_live.tm > machine().time()) {
			rollback();
			live_run(machine().time());
			pll_commit(get_floppy(), cur_live.tm);
		} else {
			pll_commit(get_floppy(), cur_live.tm);
			if(cur_live.next_state != -1) {
				cur_live.state = cur_live.next_state;
				cur_live.next_state = -1;
			}
			if(cur_live.state == IDLE) {
				pll_stop_writing(get_floppy(), cur_live.tm);
				cur_live.tm = attotime::never;
			}
		}
		cur_live.next_state = -1;
		checkpoint();
	}
}

void c8050_fdc_t::live_abort()
{
	if(!cur_live.tm.is_never() && cur_live.tm > machine().time()) {
		rollback();
		live_run(machine().time());
	}

	pll_stop_writing(get_floppy(), cur_live.tm);

	cur_live.tm = attotime::never;
	cur_live.state = IDLE;
	cur_live.next_state = -1;

	cur_live.ready = 1;
	cur_live.sync = 1;
	cur_live.error = 1;
}


void c8050_fdc_t::live_run(const attotime &limit)
{
	if(cur_live.state == IDLE || cur_live.next_state != -1)
		return;

	for(;;) {
		switch(cur_live.state) {
		case RUNNING: {
			bool syncpoint = false;

			if (cur_live.tm > limit)
				return;

			int bit = pll_get_next_bit(cur_live.tm, get_floppy(), limit);
			if(bit < 0)
				return;

			if (syncpoint) {
				live_delay(RUNNING_SYNCPOINT);
				return;
			}
			break;
		}

		case RUNNING_SYNCPOINT: {
			m_write_ready(cur_live.ready);
			m_write_sync(cur_live.sync);
			m_write_error(cur_live.error);

			cur_live.state = RUNNING;
			checkpoint();
			break;
		}
		}
	}
}

READ8_MEMBER( c8050_fdc_t::read )
{
	UINT8 e = checkpoint_live.e;
	offs_t i = checkpoint_live.i;

	UINT8 data = (BIT(e, 6) << 7) | (BIT(i, 7) << 6) | (e & 0x33) | (BIT(e, 2) << 3) | (i & 0x04);

	if (LOG) logerror("%s VIA reads data %02x (%03x)\n", machine().time().as_string(), data, checkpoint_live.shift_reg);

	return data;
}

WRITE8_MEMBER( c8050_fdc_t::write )
{
	if (m_pi != data)
	{
		live_sync();
		m_pi = cur_live.pi = data;
		checkpoint();
		if (LOG) logerror("%s PI %02x\n", machine().time().as_string(), data);
		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::drv_sel_w )
{
	if (m_drv_sel != state)
	{
		live_sync();
		m_drv_sel = cur_live.drv_sel = state;
		checkpoint();
		if (LOG) logerror("%s DRV SEL %u\n", machine().time().as_string(), state);
		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::mode_sel_w )
{
	if (m_mode_sel != state)
	{
		live_sync();
		m_mode_sel = cur_live.mode_sel = state;
		checkpoint();
		if (LOG) logerror("%s MODE SEL %u\n", machine().time().as_string(), state);
		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::rw_sel_w )
{
	if (m_rw_sel != state)
	{
		live_sync();
		m_rw_sel = cur_live.rw_sel = state;
		checkpoint();
		if (LOG) logerror("%s RW SEL %u\n", machine().time().as_string(), state);
		if (m_rw_sel) {
			pll_stop_writing(get_floppy(), machine().time());
		} else {
			pll_start_writing(machine().time());
		}
		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::mtr0_w )
{
	if (m_mtr0 != state)
	{
		live_sync();
		m_mtr0 = state;
		if (LOG) logerror("%s MTR0 %u\n", machine().time().as_string(), state);
		m_floppy0->mon_w(state);
		checkpoint();

		if (!m_mtr0 || !m_mtr1) {
			if(cur_live.state == IDLE) {
				live_start();
			}
		} else {
			live_abort();
		}

		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::mtr1_w )
{
	if (m_mtr1 != state)
	{
		live_sync();
		m_mtr1 = state;
		if (LOG) logerror("%s MTR1 %u\n", machine().time().as_string(), state);
		if (m_floppy1) m_floppy1->mon_w(state);
		checkpoint();

		if (!m_mtr0 || !m_mtr1) {
			if(cur_live.state == IDLE) {
				live_start();
			}
		} else {
			live_abort();
		}

		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::odd_hd_w )
{
	if (m_odd_hd != state)
	{
		live_sync();
		m_odd_hd = cur_live.odd_hd = state;
		if (LOG) logerror("%s ODD HD %u\n", machine().time().as_string(), state);
		m_floppy0->ss_w(!state);
		if (m_floppy1) m_floppy1->ss_w(!state);
		checkpoint();
		live_run();
	}
}

WRITE_LINE_MEMBER( c8050_fdc_t::pull_sync_w )
{
	// TODO
	if (LOG) logerror("%s PULL SYNC %u\n", machine().time().as_string(), state);
}