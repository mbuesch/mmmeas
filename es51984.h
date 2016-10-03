#ifndef ES51984_H_
#define ES51984_H_

/* Cyrustek ES 51984 digital multimeter RS232 signal interpreter. */


/** struct es51984 - ES51984 device data structure.
 * This structure is opaque to the API user. */
struct es51984;

/** enum es51984_board_type - The board type the chip is soldered onto.
 */
enum es51984_board_type {
	ES51984_BOARD_UNKNOWN,
	ES51984_BOARD_AMPROBE_35XPA,	/* The Amprobe 35XP-A multimeter */
};

#define ES51984_PACK(value)		((0x30 | (value)) & 0x7F)

/** enum es51984_func - The active device function */
enum es51984_func {
	ES51984_FUNC_VOLTAGE		= ES51984_PACK(0xB), /* Voltage measurement */
	ES51984_FUNC_UA_CURRENT		= ES51984_PACK(0xD), /* Micro-amps current measurement */
	ES51984_FUNC_MA_CURRENT		= ES51984_PACK(0xF), /* Milli-amps current measurement */
	ES51984_FUNC_AUTO_CURRENT	= ES51984_PACK(0x0), /* Auto current measurement */
	ES51984_FUNC_MAN_CURRENT	= ES51984_PACK(0x9), /* Manual current measurement */
	ES51984_FUNC_OHMS		= ES51984_PACK(0x3), /* Resistance measurement */
	ES51984_FUNC_CONT		= ES51984_PACK(0x5), /* Continuity measurement */
	ES51984_FUNC_DIODE		= ES51984_PACK(0x1), /* Diode measurement */
	ES51984_FUNC_FREQUENCY		= ES51984_PACK(0x2), /* Frequency measurement */
	ES51984_FUNC_CAPACITOR		= ES51984_PACK(0x6), /* Capacitor measurement */
	ES51984_FUNC_TEMP		= ES51984_PACK(0x4), /* Temperature measurement */
	ES51984_FUNC_ADP0		= ES51984_PACK(0xE), /* ADP0 */
	ES51984_FUNC_ADP1		= ES51984_PACK(0xC), /* ADP1 */
	ES51984_FUNC_ADP2		= ES51984_PACK(0x8), /* ADP2 */
	ES51984_FUNC_ADP3		= ES51984_PACK(0xA), /* ADP3 */
};

/** struct es51984_sample - Data sample of a measurement.
 *
 * @function: The active device function.
 * @value: The measured value.
 * @dc_mode: Boolean. DC or AC mode.
 * @auto_mode: Boolean. Automatic or manual mode.
 * @overflow: Boolean. Overflow condition present.
 * @degree: Boolean. Degree or Farenheit. Only for FUNC_TEMP.
 * @batt_low: Boolean. Battery low condition.
 * @hold: Boolean. Hold is activated. This does not influence the measurement.
 */
struct es51984_sample {
	enum es51984_func function;
	double value;
	int dc_mode;
	int auto_mode;
	int overflow;
	int degree;
	int batt_low;
	int hold;

	enum es51984_board_type board;
};

/** es51984_get_units - Get units identifier string for the value of a sample.
 * @sample: The sample.
 */
const char * es51984_get_units(const struct es51984_sample *sample);

/** es51984_get_sample - Read a sample.
 *
 * Returns zero on success, or a negative error on failure.
 * If non-blocking and no sample is available, returns -EAGAIN.
 * Returns -EPIPE, if synchronization was lost. es51984_sync() must
 * be called to resolve this error condition.
 *
 * @es: The interface.
 * @sample: Pointer to the sample buffer.
 * @blocking: If true, block until a sample arrives.
 * @debug: If true, enable debug messages.
 */
int es51984_get_sample(struct es51984 *es,
		       struct es51984_sample *sample,
		       int blocking,
		       int debug);

/** es51984_discard - Discard all pending samples
 *
 * This will discard all pending samples from the input buffer.
 * Returns -EPIPE, if the interface is not synchronized.
 *
 * @es: The interface.
 */
int es51984_discard(struct es51984 *es);

/** es51984_sync - Sync to the device.
 *
 * This will discard all pending samples and resynchronize
 * to the datastream. This must be called before requesting a sample.
 *
 * @es: The interface.
 */
int es51984_sync(struct es51984 *es);

/** es51984_init - Initialize the interface.
 * @board: The board the device is soldered onto.
 * @tty: The serial TTY device node.
 */
struct es51984 * es51984_init(enum es51984_board_type board,
			      const char *tty);

/** es51984_exit - Destroy the interface. */
void es51984_exit(struct es51984 *es);


#endif /* ES51984_H_ */
