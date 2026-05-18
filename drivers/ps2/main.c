#include <sdi.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_COMMAND_PORT 0x64u

#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u

#define PS2_CMD_READ_CONFIG 0x20u
#define PS2_CMD_WRITE_CONFIG 0x60u
#define PS2_CMD_ENABLE_FIRST_PORT 0xaeu
#define PS2_DEV_ENABLE_SCANNING 0xf4u

#define PS2_CONFIG_FIRST_IRQ 0x01u
#define PS2_CONFIG_FIRST_CLOCK_DISABLE 0x10u

typedef struct {
	sdi_irq_t irq;
	int left_shift;
	int right_shift;
	int extended;
} ps2_keyboard_t;

static const char ps2_keymap[128] = {
	[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
	[0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
	[0x0e] = '\b', [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w',
	[0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
	[0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
	[0x1a] = '[', [0x1b] = ']', [0x1c] = '\n', [0x1e] = 'a',
	[0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
	[0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
	[0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2b] = '\\',
	[0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
	[0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
	[0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char ps2_shift_keymap[128] = {
	[0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
	[0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
	[0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
	[0x0e] = '\b', [0x0f] = '\t', [0x10] = 'Q', [0x11] = 'W',
	[0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
	[0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
	[0x1a] = '{', [0x1b] = '}', [0x1c] = '\n', [0x1e] = 'A',
	[0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
	[0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
	[0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2b] = '|',
	[0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C', [0x2f] = 'V',
	[0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
	[0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

static sdi_status_t ps2_read_u8(sdi_u16 port, sdi_u8 *out)
{
	sdi_u32 value;
	sdi_status_t st = sdi_pio_read(port, 8, &value);

	if (st != SDI_OK)
		return st;

	*out = (sdi_u8)value;
	return SDI_OK;
}

static int ps2_wait_input_clear(void)
{
	for (sdi_u32 i = 0; i < 100000; i++) {
		sdi_u8 status;

		if (ps2_read_u8(PS2_STATUS_PORT, &status) != SDI_OK)
			return 0;
		if ((status & PS2_STATUS_INPUT_FULL) == 0)
			return 1;
	}

	return 0;
}

static int ps2_wait_output_full(void)
{
	for (sdi_u32 i = 0; i < 100000; i++) {
		sdi_u8 status;

		if (ps2_read_u8(PS2_STATUS_PORT, &status) != SDI_OK)
			return 0;
		if ((status & PS2_STATUS_OUTPUT_FULL) != 0)
			return 1;
	}

	return 0;
}

static sdi_status_t ps2_write_u8(sdi_u16 port, sdi_u8 value)
{
	if (!ps2_wait_input_clear())
		return SDI_ERR_TIMEOUT;

	return sdi_pio_write(port, 8, value);
}

static sdi_status_t ps2_send_device_command(sdi_u8 command, sdi_u8 *out_response)
{
	sdi_status_t st = ps2_write_u8(PS2_DATA_PORT, command);

	if (st != SDI_OK)
		return st;
	if (!ps2_wait_output_full())
		return SDI_ERR_TIMEOUT;

	return ps2_read_u8(PS2_DATA_PORT, out_response);
}

static void ps2_configure_controller(void)
{
	sdi_u8 config;

	if (ps2_write_u8(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG) != SDI_OK)
		return;
	if (ps2_read_u8(PS2_DATA_PORT, &config) != SDI_OK)
		return;

	config |= PS2_CONFIG_FIRST_IRQ;
	config &= (sdi_u8)~PS2_CONFIG_FIRST_CLOCK_DISABLE;

	if (ps2_write_u8(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG) != SDI_OK)
		return;
	(void)ps2_write_u8(PS2_DATA_PORT, config);
}

static void ps2_drain_output(void)
{
	for (sdi_u32 i = 0; i < 32; i++) {
		sdi_u8 status;
		sdi_u8 data;

		if (ps2_read_u8(PS2_STATUS_PORT, &status) != SDI_OK)
			return;
		if ((status & PS2_STATUS_OUTPUT_FULL) == 0)
			return;
		(void)ps2_read_u8(PS2_DATA_PORT, &data);
	}
}

static void ps2_log_key(char ch)
{
	char msg[] = "?";

	msg[0] = ch;
	(void)sdi_log(SDI_LOG_INFO, "ps2kbd", msg);
}

static sdi_status_t ps2_keyboard_irq(sdi_handle_t instance, sdi_irq_t irq)
{
	ps2_keyboard_t *kbd = (ps2_keyboard_t *)(sdi_uptr)instance;
	sdi_u8 status;
	sdi_u8 code;
	int released;
	char ch;

	(void)irq;

	if (!kbd)
		return SDI_ERR_INVAL;
	if (ps2_read_u8(PS2_STATUS_PORT, &status) != SDI_OK)
		return SDI_ERR_IO;
	if ((status & PS2_STATUS_OUTPUT_FULL) == 0)
		return SDI_OK;
	if (ps2_read_u8(PS2_DATA_PORT, &code) != SDI_OK)
		return SDI_ERR_IO;

	if (code == 0xfa || code == 0xfe)
		return SDI_OK;
	if (code == 0xe0) {
		kbd->extended = 1;
		return SDI_OK;
	}

	released = (code & 0x80u) != 0;
	code &= 0x7fu;

	if (!kbd->extended && code == 0x2a)
		kbd->left_shift = !released;
	else if (!kbd->extended && code == 0x36)
		kbd->right_shift = !released;
	else if (!released && !kbd->extended) {
		ch = (kbd->left_shift || kbd->right_shift) ? ps2_shift_keymap[code] :
													 ps2_keymap[code];
		if (ch != '\0')
			ps2_log_key(ch);
	}

	kbd->extended = 0;
	return SDI_OK;
}

static sdi_status_t ps2_keyboard_init(void)
{
	return SDI_OK;
}

static sdi_status_t ps2_keyboard_probe(const sdi_probe_info_t *info,
									   sdi_probe_result_t *out_result)
{
	if (!info || !out_result)
		return SDI_ERR_INVAL;

	SDI_INIT_PTR(out_result);
	out_result->score = info->match.kind == SDI_MATCH_SOFT ? 90 : 0;
	out_result->reason = "legacy PS/2 keyboard";
	return SDI_OK;
}

static sdi_status_t ps2_keyboard_attach(const sdi_attach_args_t *args,
										sdi_handle_t *out_instance)
{
	ps2_keyboard_t *kbd;

	(void)args;

	if (!out_instance)
		return SDI_ERR_INVAL;

	kbd = sdi_alloc(sizeof(*kbd), SDI_ALLOC_ZERO);
	if (!kbd)
		return SDI_ERR_NOMEM;

	*out_instance = (sdi_handle_t)(sdi_uptr)kbd;
	return SDI_OK;
}

static sdi_status_t ps2_keyboard_start(sdi_handle_t instance)
{
	ps2_keyboard_t *kbd = (ps2_keyboard_t *)(sdi_uptr)instance;
	sdi_status_t st;

	if (!kbd)
		return SDI_ERR_INVAL;

	ps2_drain_output();
	ps2_configure_controller();
	(void)ps2_write_u8(PS2_COMMAND_PORT, PS2_CMD_ENABLE_FIRST_PORT);
	{
		sdi_u8 response;

		(void)ps2_send_device_command(PS2_DEV_ENABLE_SCANNING, &response);
	}

	st = sdi_irq_bind(SDI_NULL_HANDLE, 1, 0, ps2_keyboard_irq, instance,
					  &kbd->irq);
	if (st != SDI_OK)
		return st;

	(void)sdi_irq_unmask(kbd->irq);
	return SDI_OK;
}

static sdi_status_t ps2_keyboard_stop(sdi_handle_t instance)
{
	ps2_keyboard_t *kbd = (ps2_keyboard_t *)(sdi_uptr)instance;

	if (!kbd)
		return SDI_ERR_INVAL;

	if (kbd->irq != SDI_NULL_HANDLE)
		(void)sdi_irq_unbind(kbd->irq);
	kbd->irq = SDI_NULL_HANDLE;
	return SDI_OK;
}

static sdi_status_t ps2_keyboard_detach(sdi_handle_t instance)
{
	ps2_keyboard_t *kbd = (ps2_keyboard_t *)(sdi_uptr)instance;

	if (!kbd)
		return SDI_ERR_INVAL;

	sdi_free(kbd);
	return SDI_OK;
}

static const sdi_match_id_t ps2_keyboard_matches[] = {
	{
		.size = sizeof(sdi_match_id_t),
		.version = SDI_ABI_VERSION,
		.kind = SDI_MATCH_SOFT,
		.compatible = "alix,ps2-keyboard",
	},
};

static const sdi_driver_ops_t ps2_keyboard_ops = {
	.size = sizeof(sdi_driver_ops_t),
	.version = SDI_ABI_VERSION,
	.init = ps2_keyboard_init,
	.probe = ps2_keyboard_probe,
	.attach = ps2_keyboard_attach,
	.start = ps2_keyboard_start,
	.stop = ps2_keyboard_stop,
	.detach = ps2_keyboard_detach,
};

const sdi_driver_desc_t sdi_driver = {
	.size = sizeof(sdi_driver_desc_t),
	.version = SDI_ABI_VERSION,
	.name = "ps2-keyboard",
	.vendor = "Alix",
	.description = "PS/2 keyboard SDI driver",
	.abi_major = SDI_VERSION_MAJOR,
	.abi_minor = SDI_VERSION_MINOR,
	.abi_patch = SDI_VERSION_PATCH,
	.required_caps = SDI_CAP_REQUIRED_CORE | SDI_CAP_IRQ | SDI_CAP_PIO,
	.optional_caps = 0,
	.matches = ps2_keyboard_matches,
	.match_count = SDI_ARRAY_COUNT(ps2_keyboard_matches),
	.ops = &ps2_keyboard_ops,
};
