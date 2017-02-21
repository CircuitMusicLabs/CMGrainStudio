/*
 cm.livecloud~ - a granular synthesis external audio object for Max/MSP.
 Copyright (C) 2012 - 2017  Matthias W. Müller - circuit.music.labs

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 info@circuitmusiclabs.com

 */

/************************************************************************************************************************/
/* INCLUDES                                                                                                             */
/************************************************************************************************************************/
#include "ext.h"
#include "z_dsp.h"
#include "buffer.h"
#include "ext_atomic.h"
#include "ext_obex.h"
#include <stdlib.h> // for arc4random_uniform
#include <math.h> // for stereo functions
#define MAX_GRAINLENGTH 500 // max grain length in ms
#define MIN_GRAINLENGTH 1 // min grain length in ms
#define MIN_PITCH 0.001 // min pitch
#define MAX_PITCH 4 // max pitch
#define MIN_PAN -1.0 // min pan
#define MAX_PAN 1.0 // max pan
#define MIN_GAIN 0.0 // min gain
#define MAX_GAIN 2.0  // max gain
#define ARGUMENTS 2 // constant number of arguments required for the external
#define FLOAT_INLETS 10 // number of object float inlets
#define RANDMAX 10000
#define BUFFERMS 2000


/************************************************************************************************************************/
/* GRAIN MEMORY STORAGE                                                                                                 */
/************************************************************************************************************************/
typedef struct cmcloud {
	double *left;
	double *right;
	long length;
	long pos;
	short busy; // used to store the flag if a grain is currently playing or not
} cm_cloud;


/************************************************************************************************************************/
/* OBJECT STRUCTURE                                                                                                     */
/************************************************************************************************************************/
typedef struct _cmlivecloud {
	t_pxobject obj;
	t_symbol *window_name; // window buffer name
	t_buffer_ref *w_buffer; // window buffer reference
	double m_sr; // system millisampling rate (samples per milliseconds = sr * 0.001)
	short connect_status[FLOAT_INLETS]; // array for signal inlet connection statuses
	double *object_inlets; // array to store the incoming values coming from the object inlets
	double *grain_params; // array to store the processed values coming from the object inlets
	double *randomized; // array to store the randomized grain values
	double *testvalues; // array for storing the grain parameter test values (sanity testing)
	double tr_prev; // trigger sample from previous signal vector (required to check if input ramp resets to zero)
	short buffer_modified; // checkflag to see if buffer has been modified
	short grains_count; // currently playing grains
	void *grains_count_out; // outlet for number of currently playing grains (for debugging)
	void *rec_position_out; // outlet for current record position in buffer
	t_atom_long attr_winterp; // attribute: window interpolation on/off
	t_atom_long attr_sinterp; // attribute: window interpolation on/off
	t_atom_long attr_zero; // attribute: zero crossing trigger on/off
	double piovr2; // pi over two for panning function
	double root2ovr2; // root of 2 over two for panning function
	double *ringbuffer; // circular buffer for recording the audio input
	long bufferframes; // size of buffer in samples
	long writepos; // buffer write position
	short record; // record on/off flag from "record" method
	short recordflag; // boolean to indicate that recording has been started (disables recording until all currently playing grains have finished
	short bang_trigger; // trigger received from bang method
	cm_cloud *cloud; // struct array for storing the grains and associated variables in memory
	long cloudsize; // size of the cloud struct array, value obtained from argument and "cloudsize" method
	t_bool resize_request; // flag set to true when "cloudsize" method called
	long cloudsize_new; // new cloudsize obtained from "cloudsize" method
	t_bool resize_verify; // check-flag for proper memroy re-allocation
} t_cmlivecloud;


/************************************************************************************************************************/
/* STEREO STRUCTURE                                                                                                     */
/************************************************************************************************************************/
typedef struct cmpanner {
	double left;
	double right;
} cm_panstruct;


/************************************************************************************************************************/
/* STATIC DECLARATIONS                                                                                                  */
/************************************************************************************************************************/
static t_class *cmlivecloud_class; // class pointer
static t_symbol *ps_buffer_modified, *ps_stereo;


/************************************************************************************************************************/
/* FUNCTION PROTOTYPES                                                                                                  */
/************************************************************************************************************************/
void *cmlivecloud_new(t_symbol *s, long argc, t_atom *argv);
void cmlivecloud_dsp64(t_cmlivecloud *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void cmlivecloud_perform64(t_cmlivecloud *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);
void cmlivecloud_assist(t_cmlivecloud *x, void *b, long msg, long arg, char *dst);
void cmlivecloud_free(t_cmlivecloud *x);
void cmlivecloud_float(t_cmlivecloud *x, double f);
void cmlivecloud_dblclick(t_cmlivecloud *x);
t_max_err cmlivecloud_notify(t_cmlivecloud *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void cmlivecloud_set(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av);
void cmlivecloud_cloudsize(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av);
void cmlivecloud_record(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av);
void cmlivecloud_bang(t_cmlivecloud *x);
t_max_err cmlivecloud_stereo_set(t_cmlivecloud *x, t_object *attr, long argc, t_atom *argv);
t_max_err cmlivecloud_winterp_set(t_cmlivecloud *x, t_object *attr, long argc, t_atom *argv);
t_max_err cmlivecloud_sinterp_set(t_cmlivecloud *x, t_object *attr, long argc, t_atom *argv);
t_max_err cmlivecloud_zero_set(t_cmlivecloud *x, t_object *attr, long argc, t_atom *argv);
t_bool cmlivecloud_resize(t_cmlivecloud *x);

// PANNING FUNCTION
void cm_panning(cm_panstruct *panstruct, double *pos, t_cmlivecloud *x);
// RANDOM NUMBER GENERATOR
double cm_random(double *min, double *max);
// LINEAR INTERPOLATION FUNCTION
double cm_lininterp(double distance, float *b_sample, t_atom_long b_channelcount, t_atom_long b_framecount, short channel);
double cm_lininterpring(double distance, long index, long next, double *ringbuffer);


/************************************************************************************************************************/
/* MAIN FUNCTION (INITIALIZATION ROUTINE)                                                                               */
/************************************************************************************************************************/
void ext_main(void *r) {
	// Initialize the class - first argument: VERY important to match the name of the object in the procect settings!!!
	cmlivecloud_class = class_new("cm.livecloud~", (method)cmlivecloud_new, (method)cmlivecloud_free, sizeof(t_cmlivecloud), 0, A_GIMME, 0);

	class_addmethod(cmlivecloud_class, (method)cmlivecloud_dsp64, 		"dsp64", 	A_CANT, 0);  // Bind the 64 bit dsp method
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_assist, 		"assist", 	A_CANT, 0); // Bind the assist message
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_float, 		"float", 	A_FLOAT, 0); // Bind the float message (allowing float input)
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_dblclick, 	"dblclick",	A_CANT, 0); // Bind the double click message
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_notify, 		"notify", 	A_CANT, 0); // Bind the notify message
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_set, 		"set", 		A_GIMME, 0); // Bind the set message for user buffer set
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_cloudsize,	"cloudsize",A_GIMME, 0); // Bind the limit message
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_record, 		"record", 	A_GIMME, 0); // Bind the limit message
	class_addmethod(cmlivecloud_class, (method)cmlivecloud_bang,		"bang",		0);

	CLASS_ATTR_ATOM_LONG(cmlivecloud_class, "w_interp", 0, t_cmlivecloud, attr_winterp);
	CLASS_ATTR_ACCESSORS(cmlivecloud_class, "w_interp", (method)NULL, (method)cmlivecloud_winterp_set);
	CLASS_ATTR_BASIC(cmlivecloud_class, "w_interp", 0);
	CLASS_ATTR_SAVE(cmlivecloud_class, "w_interp", 0);
	CLASS_ATTR_STYLE_LABEL(cmlivecloud_class, "w_interp", 0, "onoff", "Window interpolation on/off");

	CLASS_ATTR_ATOM_LONG(cmlivecloud_class, "s_interp", 0, t_cmlivecloud, attr_sinterp);
	CLASS_ATTR_ACCESSORS(cmlivecloud_class, "s_interp", (method)NULL, (method)cmlivecloud_sinterp_set);
	CLASS_ATTR_BASIC(cmlivecloud_class, "s_interp", 0);
	CLASS_ATTR_SAVE(cmlivecloud_class, "s_interp", 0);
	CLASS_ATTR_STYLE_LABEL(cmlivecloud_class, "s_interp", 0, "onoff", "Sample interpolation on/off");

	CLASS_ATTR_ATOM_LONG(cmlivecloud_class, "zero", 0, t_cmlivecloud, attr_zero);
	CLASS_ATTR_ACCESSORS(cmlivecloud_class, "zero", (method)NULL, (method)cmlivecloud_zero_set);
	CLASS_ATTR_BASIC(cmlivecloud_class, "zero", 0);
	CLASS_ATTR_SAVE(cmlivecloud_class, "zero", 0);
	CLASS_ATTR_STYLE_LABEL(cmlivecloud_class, "zero", 0, "onoff", "Zero crossing trigger mode on/off");

	CLASS_ATTR_ORDER(cmlivecloud_class, "w_interp", 0, "1");
	CLASS_ATTR_ORDER(cmlivecloud_class, "s_interp", 0, "2");
	CLASS_ATTR_ORDER(cmlivecloud_class, "zero", 0, "3");

	class_dspinit(cmlivecloud_class); // Add standard Max/MSP methods to your class
	class_register(CLASS_BOX, cmlivecloud_class); // Register the class with Max
	ps_buffer_modified = gensym("buffer_modified"); // assign the buffer modified message to the static pointer created above
	ps_stereo = gensym("stereo");
}


/************************************************************************************************************************/
/* NEW INSTANCE ROUTINE                                                                                                 */
/************************************************************************************************************************/
void *cmlivecloud_new(t_symbol *s, long argc, t_atom *argv) {
	int i;
	t_cmlivecloud *x = (t_cmlivecloud *)object_alloc(cmlivecloud_class); // create the object and allocate required memory
	dsp_setup((t_pxobject *)x, 12); // create 12 inlets


	if (argc < ARGUMENTS) {
		object_error((t_object *)x, "%d arguments required (window buffer / cloud size)", ARGUMENTS);
		return NULL;
	}

	x->window_name = atom_getsymarg(0, argc, argv); // get user supplied argument for window buffer
	x->cloudsize = atom_getintarg(1, argc, argv); // get user supplied argument for maximum grains

	// HANDLE ATTRIBUTES
	object_attr_setlong(x, gensym("w_interp"), 0); // initialize window interpolation attribute
	object_attr_setlong(x, gensym("s_interp"), 1); // initialize window interpolation attribute
	object_attr_setlong(x, gensym("zero"), 0); // initialize zero crossing attribute
	attr_args_process(x, argc, argv); // get attribute values if supplied as argument

	// CHECK IF USER SUPPLIED MAXIMUM GRAINS IS IN THE LEGAL RANGE (larger than 1)
	if (x->cloudsize < 1) {
		object_error((t_object *)x, "cloud size must be larger than 1");
		return NULL;
	}

	// CREATE OUTLETS (OUTLETS ARE CREATED FROM RIGHT TO LEFT)
	x->grains_count_out = intout((t_object *)x); // create outlet for number of currently playing grains
	x->rec_position_out = intout((t_object *)x); // create outlet for current record position in buffer
	outlet_new((t_object *)x, "signal"); // right signal outlet
	outlet_new((t_object *)x, "signal"); // left signal outlet

	// GET SYSTEM SAMPLE RATE
	x->m_sr = sys_getsr() * 0.001; // get the current sample rate and write it into the object structure

	/************************************************************************************************************************/
	// ALLOCATE MEMORY FOR THE OBJET FLOAT_INLETS ARRAY
	x->object_inlets = (double *)sysmem_newptrclear((FLOAT_INLETS) * sizeof(double));
	if (x->object_inlets == NULL) {
		object_error((t_object *)x, "out of memory");
		return NULL;
	}

	// ALLOCATE MEMORY FOR THE GRAIN PARAMETERS ARRAY
	x->grain_params = (double *)sysmem_newptrclear((FLOAT_INLETS) * sizeof(double));
	if (x->grain_params == NULL) {
		object_error((t_object *)x, "out of memory");
		return NULL;
	}

	// ALLOCATE MEMORY FOR THE GRAIN PARAMETERS ARRAY
	x->randomized = (double *)sysmem_newptrclear((FLOAT_INLETS / 2) * sizeof(double)); // 5 pairs of parameters to randomize
	if (x->randomized == NULL) {
		object_error((t_object *)x, "out of memory");
		return NULL;
	}

	// ALLOCATE MEMORY FOR THE TEST VALUES ARRAY
	x->testvalues = (double *)sysmem_newptrclear((FLOAT_INLETS) * sizeof(double));
	if (x->testvalues == NULL) {
		object_error((t_object *)x, "out of memory");
		return NULL;
	}

	x->ringbuffer = (double *)sysmem_newptrclear((BUFFERMS * x->m_sr) * sizeof(double));
	if (x->ringbuffer == NULL) {
		object_error((t_object *)x, "out of memory");
		return NULL;
	}
	
	// ALLOCATE MEMORY FOR THE cloud ARRAY
	x->cloud = (cm_cloud *)sysmem_newptrclear((x->cloudsize) * sizeof(cm_cloud));
	if (x->cloud == NULL) {
		object_error((t_object *)x, "out of memory");
		return NULL;
	}
	
	// ALLOCATE MEMORY FOR THE GRAIN ARRAY IN EACH MEMBER OF THE cloud STRUCT
	for (i = 0; i < x->cloudsize; i++) {
		x->cloud[i].left = (double *)sysmem_newptrclear(((MAX_GRAINLENGTH * x->m_sr) * MAX_PITCH) * sizeof(double));
		if (x->cloud[i].left == NULL) {
			object_error((t_object *)x, "out of memory");
			return NULL;
		}
		x->cloud[i].right = (double *)sysmem_newptrclear(((MAX_GRAINLENGTH * x->m_sr) * MAX_PITCH) * sizeof(double));
		if (x->cloud[i].right == NULL) {
			object_error((t_object *)x, "out of memory");
			return NULL;
		}
	}


	
	/************************************************************************************************************************/
	// INITIALIZE VALUES
	x->object_inlets[0] = 0.0; // initialize float inlet value for min delay
	x->object_inlets[1] = 0.0; // initialize float inlet value for max delay
	x->object_inlets[2] = 150; // initialize float inlet value for min grain length
	x->object_inlets[3] = 150; // initialize float inlet value for max grain length
	x->object_inlets[4] = 1.0; // initialize inlet value for min pitch
	x->object_inlets[5] = 1.0; // initialize inlet value for min pitch
	x->object_inlets[6] = 0.0; // initialize value for min pan
	x->object_inlets[7] = 0.0; // initialize value for max pan
	x->object_inlets[8] = 1.0; // initialize value for min gain
	x->object_inlets[9] = 1.0; // initialize value for max gain
	x->tr_prev = 0.0; // initialize value for previous trigger sample
	x->grains_count = 0; // initialize the grains count value
	x->buffer_modified = 0; // initialized buffer modified flag
	// initialize the testvalues which are not dependent on sampleRate
	x->testvalues[3] = MIN_PITCH;
	x->testvalues[4] = MAX_PITCH;
	x->testvalues[5] = MIN_PAN;
	x->testvalues[6] = MAX_PAN;
	x->testvalues[7] = MIN_GAIN;
	x->testvalues[8] = MAX_GAIN;

	x->writepos = 0;
	x->bufferframes = BUFFERMS * x->m_sr;
	x->recordflag = 0;

	// calculate constants for panning function
	x->piovr2 = 4.0 * atan(1.0) * 0.5;
	x->root2ovr2 = sqrt(2.0) * 0.5;
	
	// bang trigger flag
	x->bang_trigger = 0;
	
	// cloud structure members
	for (i = 0; i < x->cloudsize; i++) {
		x->cloud[i].length = 0;
		x->cloud[i].pos = 0;
		x->cloud[i].busy = 0;
	}
	
	x->resize_request = false;
	x->resize_verify = false;
	x->recordflag = 0;

	/************************************************************************************************************************/
	// BUFFER REFERENCES
	x->w_buffer = buffer_ref_new((t_object *)x, x->window_name); // write the window buffer reference into the object structure

	#ifdef WIN_VERSION
		srand((unsigned int)clock());
	#endif

	return x;
}


/************************************************************************************************************************/
/* THE 64 BIT DSP METHOD                                                                                                */
/************************************************************************************************************************/
void cmlivecloud_dsp64(t_cmlivecloud *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
	int i;
	x->connect_status[0] = count[2]; // signal connect status:	delay min
	x->connect_status[1] = count[3]; // signal connect status:	delay max
	x->connect_status[2] = count[4]; // signal connect status:	length min
	x->connect_status[3] = count[5]; // signal connect status:	length max
	x->connect_status[4] = count[6]; // signal connect status:	pitch min
	x->connect_status[5] = count[7]; // signal connect status:	pitch max
	x->connect_status[6] = count[8]; // signal connect status:	pan min
	x->connect_status[7] = count[9]; // signal connect status:	pan max
	x->connect_status[8] = count[10]; // signal connect status:	gain min
	x->connect_status[9] = count[11]; // signal connect status:	gain max

	if (x->m_sr != samplerate * 0.001) { // check if sample rate stored in object structure is the same as the current project sample rate
		x->m_sr = samplerate * 0.001;
		for (i = 0; i < x->cloudsize; i++) {
			x->cloud[i].left = (double *)sysmem_resizeptrclear(x->cloud[i].left, ((MAX_GRAINLENGTH * x->m_sr) * MAX_PITCH) * sizeof(double));
			if (x->cloud[i].left == NULL) {
				object_error((t_object *)x, "out of memory");
				return;
			}
		}
		for (i = 0; i < x->cloudsize; i++) {
			x->cloud[i].right = (double *)sysmem_resizeptrclear(x->cloud[i].right, ((MAX_GRAINLENGTH * x->m_sr) * MAX_PITCH) * sizeof(double));
			if (x->cloud[i].right == NULL) {
				object_error((t_object *)x, "out of memory");
				return;
			}
		}
		x->ringbuffer = (double *)sysmem_resizeptrclear(x->ringbuffer, (BUFFERMS * x->m_sr) * sizeof(double));
		if (x->ringbuffer == NULL) {
			object_error((t_object *)x, "out of memory");
			return;
		}
	}
	// calcuate the sampleRate-dependant test values
	x->testvalues[0] = BUFFERMS * x->m_sr; // max delay (min delay = 0)
	x->testvalues[1] = (MIN_GRAINLENGTH) * x->m_sr; // min grain length
	x->testvalues[2] = (MAX_GRAINLENGTH) * x->m_sr; // max grain legnth

	x->bufferframes = BUFFERMS * x->m_sr;

	// CALL THE PERFORM ROUTINE
	object_method(dsp64, gensym("dsp_add64"), x, cmlivecloud_perform64, 0, NULL);
}


/************************************************************************************************************************/
/* THE 64 BIT PERFORM ROUTINE                                                                                           */
/************************************************************************************************************************/
void cmlivecloud_perform64(t_cmlivecloud *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam) {
	// VARIABLE DECLARATIONS
	short trigger = 0; // trigger occurred yes/no
	long i, r; // for loop counterS
	long n = sampleframes; // number of samples per signal vector
	double tr_curr, sig_curr; // current trigger and signal value
	double distance; // floating point index for reading from buffers
	long next;
	long index; // truncated index for reading from buffers
	double w_read, b_read; // current sample read from the window buffer
	double outsample_left = 0.0; // temporary left output sample used for adding up all grain samples
	double outsample_right = 0.0; // temporary right output sample used for adding up all grain samples
	int slot = 0; // variable for the current slot in the arrays to write grain info to
	cm_panstruct panstruct; // struct for holding the calculated constant power left and right stereo values
	
	long readpos;
	double start;
	double smp_length;
	double pitch_length;
	double gain;
	double pan_left, pan_right;
	long max_delay; // calculated maximum delay length according to grain length and pitch

	// OUTLETS
	t_double *out_left 	= (t_double *)outs[0]; // assign pointer to left output
	t_double *out_right = (t_double *)outs[1]; // assign pointer to right output
	
	
	// MEMORY RESIZE
	if (x->grains_count == 0 && x->resize_request) {
		// allocate new memory and check if all went well
		x->resize_verify = cmlivecloud_resize(x);
		if (x->resize_verify) { // if all OK
			x->resize_verify = false;
			x->resize_request = false;
		}
		else {
			// if mem-allocation fails, go to zero and try again next time:
			// x->resize_request is not reset
			goto zero;
		}
	}

	// BUFFER VARIABLE DECLARATIONS
	t_buffer_obj *w_buffer = buffer_ref_getobject(x->w_buffer);
	float *w_sample = buffer_locksamples(w_buffer);
	long w_framecount; // number of frames in the window buffer
	t_atom_long w_channelcount; // number of channels in the window buffer

	// BUFFER CHECKS
	if (!w_sample) { // if the sample buffer does not exist
		goto zero;
	}

	// GET BUFFER INFORMATION
	w_framecount = buffer_getframecount(w_buffer); // get number of frames in the window buffer
	w_channelcount = buffer_getchannelcount(w_buffer); // get number of channels in the sample buffer

	// GET INLET VALUES
	t_double *tr_sigin		= (t_double *)ins[0]; // get trigger input signal from 1st inlet
	t_double *rec_sigin	= (t_double *)ins[1]; // get trigger input signal from 1st inlet

	x->grain_params[0] = x->connect_status[0] ? *ins[2] * x->m_sr : x->object_inlets[0] * x->m_sr;	// delay min
	x->grain_params[1] = x->connect_status[1] ? *ins[3] * x->m_sr : x->object_inlets[1] * x->m_sr;	// delay max
	x->grain_params[2] = x->connect_status[2] ? *ins[4] * x->m_sr : x->object_inlets[2] * x->m_sr;	// length min
	x->grain_params[3] = x->connect_status[3] ? *ins[5] * x->m_sr : x->object_inlets[3] * x->m_sr;	// length max
	x->grain_params[4] = x->connect_status[4] ? *ins[6] : x->object_inlets[4];						// pitch min
	x->grain_params[5] = x->connect_status[5] ? *ins[7] : x->object_inlets[5];						// pitch max
	x->grain_params[6] = x->connect_status[6] ? *ins[8] : x->object_inlets[6];						// pan min
	x->grain_params[7] = x->connect_status[7] ? *ins[9] : x->object_inlets[7];						// pan max
	x->grain_params[8] = x->connect_status[8] ? *ins[10] : x->object_inlets[8];						// gain min
	x->grain_params[9] = x->connect_status[9] ? *ins[11] : x->object_inlets[9];						// gain max

	// DSP LOOP
	while (n--) {
		tr_curr = *tr_sigin++; // get current trigger value
		sig_curr = *rec_sigin++; // get current signal value

		// WRITE INTO RINGBUFFER:
		if (x->record) {
			x->ringbuffer[x->writepos++] = sig_curr;
			if (x->writepos == x->bufferframes) {
				x->writepos = 0;
			}
		}


		// process trigger value
		if (x->attr_zero) { // if zero crossing attr is set
			if (signbit(tr_curr) != signbit(x->tr_prev)) { // zero crossing from negative to positive
				trigger = 1;
			}
			else if (x->bang_trigger) {
				trigger = 1;
				x->bang_trigger = 0;
			}
		}
		else { // if zero crossing attr is not set
			if ((x->tr_prev - tr_curr) > 0.9) {
				trigger = 1;
			}
			else if (x->bang_trigger) {
				trigger = 1;
				x->bang_trigger = 0;
			}
		}

		/************************************************************************************************************************/
		// IN CASE OF TRIGGER, LIMIT NOT MODIFIED AND GRAINS COUNT IN THE LEGAL RANGE (AVAILABLE SLOTS)
		if (trigger && x->grains_count < x->cloudsize && !x->resize_request && !x->recordflag && !x->buffer_modified && w_sample) {

			trigger = 0; // reset trigger
			x->grains_count++; // increment grains_count

			// FIND A FREE SLOT FOR THE NEW GRAIN
			i = 0;
			while (i < x->cloudsize) {
				if (!x->cloud[i].busy) {
					x->cloud[i].busy = 1;
					slot = i;
					break;
				}
				i++;
			}

			// randomize the grain parameters and write them into the randomized array
			x->randomized[0] = cm_random(&x->grain_params[0], &x->grain_params[1]); // delay
			x->randomized[1] = cm_random(&x->grain_params[2], &x->grain_params[3]); // length
			x->randomized[2] = cm_random(&x->grain_params[4], &x->grain_params[5]); // pitch
			x->randomized[3] = cm_random(&x->grain_params[6], &x->grain_params[7]); // pan
			x->randomized[4] = cm_random(&x->grain_params[8], &x->grain_params[9]); // gain

			// check for parameter sanity for delay value
			if (x->randomized[0] < 0) {
				x->randomized[0] = 0;
			}
			else if (x->randomized[0] > x->testvalues[0]) {
				x->randomized[0] = x->testvalues[0];
			}

			// check for parameter sanity for length value
			if (x->randomized[1] < x->testvalues[1]) {
				x->randomized[1] = x->testvalues[1];
			}
			else if (x->randomized[1] > x->testvalues[2]) {
				x->randomized[1] = x->testvalues[2];
			}

			// check for parameter sanity for pitch value
			if (x->randomized[2] < x->testvalues[3]) {
				x->randomized[2] = x->testvalues[3];
			}
			else if (x->randomized[2] > x->testvalues[4]) {
				x->randomized[2] = x->testvalues[4];
			}

			// check for parameter sanity for pan value
			if (x->randomized[3] < x->testvalues[5]) {
				x->randomized[3] = x->testvalues[5];
			}
			else if (x->randomized[3] > x->testvalues[6]) {
				x->randomized[3] = x->testvalues[6];
			}

			// check for parameter sanity for gain value
			if (x->randomized[4] < x->testvalues[7]) {
				x->randomized[4] = x->testvalues[7];
			}
			else if (x->randomized[4] > x->testvalues[8]) {
				x->randomized[4] = x->testvalues[8];
			}

			// write grain length in samples (non-pitch)
			smp_length = x->randomized[1];
			pitch_length = smp_length * x->randomized[2]; // length * pitch

			// check that grain length is not larger than size of buffer
			if (pitch_length > x->bufferframes) {
				pitch_length = x->bufferframes;
			}

			// calculate the maximum delay value according to the actual grain length
			// in order to avoid running over the record position
			max_delay = x->bufferframes - pitch_length;
			// adjust delay according to the above calculation
			if (x->randomized[0] > max_delay) {
				x->randomized[0] = max_delay;
			}

			// compute pan values
			cm_panning(&panstruct, &x->randomized[3], x); // calculate pan values in panstruct
			pan_left = panstruct.left;
			pan_right = panstruct.right;

			// write gain value
			gain = x->randomized[4];

			start = x->writepos - pitch_length;
			if (start < 0) {
				start = start * -1;
				start = x->bufferframes - start;
			}

			start -= x->randomized[0];
			if (start < 0) {
				start = start * -1;
				start = x->bufferframes - start;
			}
			x->cloud[slot].length = smp_length; // IMPORTANT!! DO NOT FORGET TO WRITE THE SAMPLE LENGTH INTO THE MEMORY STRUCTURE

			for (readpos = 0; readpos < smp_length; readpos++) {
				if (x->attr_winterp) {
					distance = ((double)readpos / (double)smp_length) * (double)w_framecount;
					w_read = cm_lininterp(distance, w_sample, w_channelcount, w_framecount, 0);
				}
				else {
					index = (long)(((double)readpos / (double)smp_length) * (double)w_framecount);
					w_read = w_sample[index];
				}

				if (x->attr_sinterp) {
					distance = (double)start + (((double)readpos / (double)smp_length) * (double)pitch_length);
					index = (long)distance; // get truncated index
					next = index + 1;
					distance -= (long)distance; // calculate fraction value for interpolation
					if (index >= x->bufferframes) {
						index -= x->bufferframes;
					}
					if (next >= x->bufferframes) {
						next -= x->bufferframes;
					}
					b_read = cm_lininterpring(distance, index, next, x->ringbuffer) * w_read; // get interpolated sample
					x->cloud[slot].left[readpos] = (b_read * pan_left) * gain;
					x->cloud[slot].right[readpos] = (b_read * pan_right) * gain;
				}
				else {
					index = (long)((double)start + (((double)readpos / (double)smp_length) * (double)pitch_length));
					if (index >= x->bufferframes) {
						index -= x->bufferframes;
					}
					x->cloud[slot].left[readpos] = ((x->ringbuffer[index] * w_read) * pan_left) * gain;
					x->cloud[slot].right[readpos] = ((x->ringbuffer[index] * w_read) * pan_right) * gain;
				}
			}
		}
		/************************************************************************************************************************/
		// CONTINUE WITH THE PLAYBACK ROUTINE
		
		// playback only if there are grains to play
		if (x->grains_count) {
			for (i = 0; i < x->cloudsize; i++) {
				if (x->cloud[i].busy) {
					r = x->cloud[i].pos++;
					outsample_left += x->cloud[i].left[r];
					outsample_right += x->cloud[i].right[r];
					if (x->cloud[i].pos == x->cloud[i].length) {
						x->cloud[i].pos = 0;
						x->cloud[i].busy = 0;
						x->grains_count--;
						if (x->grains_count < 0) {
							x->grains_count = 0;
						}
					}
				}
			}
		}
		// CHECK IF GRAINS COUNT IS ZERO, THEN RESET LIMIT_MODIFIED CHECKFLAG
		if (x->grains_count == 0) {
			if (x->buffer_modified) {
				x->buffer_modified = 0;
			}
			if (x->recordflag) {
				x->recordflag = 0;
			}
		}

		/************************************************************************************************************************/
		x->tr_prev = tr_curr; // store current trigger value in object structure

		*out_left++ = outsample_left; // write added sample values to left output vector
		*out_right++ = outsample_right; // write added sample values to right output vector
		
		outsample_left = 0.0;
		outsample_right = 0.0;
	}

	/************************************************************************************************************************/
	// STORE UPDATED RUNNING VALUES INTO THE OBJECT STRUCTURE
	buffer_unlocksamples(w_buffer);
//	if (x->randomized[0] == x->bufferframes) {
//		x->randomized[0] = 0;
//	}
	outlet_int(x->grains_count_out, x->grains_count); // send number of currently playing grains to the outlet
	outlet_int(x->rec_position_out, x->writepos / x->m_sr); // send current record position to the outlet
	return;

zero:
	while (n--) {
		*out_left++ = 0.0;
		*out_right++ = 0.0;
	}
	buffer_unlocksamples(w_buffer);
	return; // THIS RETURN WAS MISSING FOR A LONG, LONG TIME. MAYBE THIS HELPS WITH STABILITY!?
}


/************************************************************************************************************************/
/* ASSIST METHOD FOR INLET AND OUTLET ANNOTATION                                                                        */
/************************************************************************************************************************/
void cmlivecloud_assist(t_cmlivecloud *x, void *b, long msg, long arg, char *dst) {
	if (msg == ASSIST_INLET) {
		switch (arg) {
			case 0:
				snprintf_zero(dst, 256, "(signal) trigger in");
				break;
			case 1:
				snprintf_zero(dst, 256, "(signal) audio input");
				break;
			case 2:
				snprintf_zero(dst, 256, "(signal/float) min delay");
				break;
			case 3:
				snprintf_zero(dst, 256, "(signal/float) max delay");
				break;
			case 4:
				snprintf_zero(dst, 256, "(signal/float) min grain length");
				break;
			case 5:
				snprintf_zero(dst, 256, "(signal/float) max grain length");
				break;
			case 6:
				snprintf_zero(dst, 256, "(signal/float) pitch min");
				break;
			case 7:
				snprintf_zero(dst, 256, "(signal/float) pitch max");
				break;
			case 8:
				snprintf_zero(dst, 256, "(signal/float) pan min");
				break;
			case 9:
				snprintf_zero(dst, 256, "(signal/float) pan max");
				break;
			case 10:
				snprintf_zero(dst, 256, "(signal/float) gain min");
				break;
			case 11:
				snprintf_zero(dst, 256, "(signal/float) gain max");
				break;
		}
	}
	else if (msg == ASSIST_OUTLET) {
		switch (arg) {
			case 0:
				snprintf_zero(dst, 256, "(signal) output ch1");
				break;
			case 1:
				snprintf_zero(dst, 256, "(signal) output ch2");
				break;
			case 2:
				snprintf_zero(dst, 256, "(int) current recording position");
				break;
			case 3:
				snprintf_zero(dst, 256, "(int) current grain count");
				break;
		}
	}
}


/************************************************************************************************************************/
/* FREE FUNCTION                                                                                                        */
/************************************************************************************************************************/
void cmlivecloud_free(t_cmlivecloud *x) {
	int i;
	dsp_free((t_pxobject *)x); // free memory allocated for the object
	object_free(x->w_buffer); // free the window buffer reference
	sysmem_freeptr(x->object_inlets); // free memory allocated to the object inlets array
	sysmem_freeptr(x->grain_params); // free memory allocated to the grain parameters array
	sysmem_freeptr(x->randomized); // free memory allocated to the grain parameters array
	sysmem_freeptr(x->testvalues); // free memory allocated to the test values array

	for (i = 0; i < x->cloudsize; i++) {
		sysmem_freeptr(x->cloud[i].left);
		sysmem_freeptr(x->cloud[i].right);
	}
	sysmem_freeptr(x->cloud);

}

/************************************************************************************************************************/
/* FLOAT METHOD FOR FLOAT INLET SUPPORT                                                                                 */
/************************************************************************************************************************/
void cmlivecloud_float(t_cmlivecloud *x, double f) {
	double dump;
	int inlet = ((t_pxobject*)x)->z_in; // get info as to which inlet was addressed (stored in the z_in component of the object structure
	switch (inlet) {
		case 2: // delay min
			if (f < 0.0 || f > BUFFERMS) {
				dump = f;
			}
			else {
				x->object_inlets[0] = f;
			}
			break;

		case 3: // delay max
			if (f < 0.0 || f > BUFFERMS) {
				dump = f;
			}
			else {
				x->object_inlets[1] = f;
			}
			break;

		case 4: // length min
			if (f < MIN_GRAINLENGTH || f > MAX_GRAINLENGTH) {
				dump = f;
			}
			else {
				x->object_inlets[2] = f;
			}
			break;

		case 5: // length max
			if (f < MIN_GRAINLENGTH || f > MAX_GRAINLENGTH) {
				dump = f;
			}
			else {
				x->object_inlets[3] = f;
			}
			break;

		case 6: // pitch min
			if (f <= 0.0 || f > MAX_PITCH) {
				dump = f;
			}
			else {
				x->object_inlets[4] = f;
			}
			break;

		case 7: // pitch max
			if (f <= 0.0 || f > MAX_PITCH) {
				dump = f;
			}
			else {
				x->object_inlets[5] = f;
			}
			break;

		case 8: // pan min
			if (f < -1.0 || f > 1.0) {
				dump = f;
			}
			else {
				x->object_inlets[6] = f;
			}
			break;

		case 9: // pan max
			if (f < -1.0 || f > 1.0) {
				dump = f;
			}
			else {
				x->object_inlets[7] = f;
			}
			break;

		case 10: // gain min
			if (f < 0.0 || f > MAX_GAIN) {
				dump = f;
			}
			else {
				x->object_inlets[8] = f;
			}
			break;

		case 11: // gain max
			if (f < 0.0 || f > MAX_GAIN) {
				dump = f;
			}
			else {
				x->object_inlets[9] = f;
			}
			break;
	}
}


/************************************************************************************************************************/
/* DOUBLE CLICK METHOD FOR VIEWING BUFFER CONTENT                                                                       */
/************************************************************************************************************************/
void cmlivecloud_dblclick(t_cmlivecloud *x) {
	buffer_view(buffer_ref_getobject(x->w_buffer));
}


/************************************************************************************************************************/
/* NOTIFY METHOD FOR THE BUFFER REFERENCES                                                                              */
/************************************************************************************************************************/
t_max_err cmlivecloud_notify(t_cmlivecloud *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
	t_symbol *buffer_name = (t_symbol *)object_method((t_object *)sender, gensym("getname"));
	if (msg == ps_buffer_modified) {
		x->buffer_modified = 1;
	}
	if (buffer_name == x->window_name) { // check if calling object was the window buffer
		return buffer_ref_notify(x->w_buffer, s, msg, sender, data); // return with the calling buffer
	}
	else { // if calling object was none of the expected buffers
		return MAX_ERR_NONE; // return generic MAX_ERR_NONE
	}
}


/************************************************************************************************************************/
/* THE BUFFER SET METHOD                                                                                                */
/************************************************************************************************************************/
void cmlivecloud_doset(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av) {
	if (ac == 1) {
		x->buffer_modified = 1;
		x->window_name = atom_getsym(av); // write buffer name into object structure
		buffer_ref_set(x->w_buffer, x->window_name);
		if (buffer_getchannelcount((t_object *)(buffer_ref_getobject(x->w_buffer))) > 1) {
			object_error((t_object *)x, "referenced window buffer has more than 1 channel. expect strange results.");
		}
	}
	else {
		object_error((t_object *)x, "%d arguments required (window)", 1);
	}
}


/************************************************************************************************************************/
/* THE BUFFER SET METHOD                                                                                                */
/************************************************************************************************************************/
void cmlivecloud_set(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av) {
	defer(x, (method)cmlivecloud_doset, s, ac, av);
}


/************************************************************************************************************************/
/* THE RESIZE REQUEST METHOD                                                                                            */
/************************************************************************************************************************/
void cmlivecloud_cloudsize(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av) {
	long arg;
	arg = atom_getlong(av);
	if (arg < 1) {
		object_error((t_object *)x, "cloud size must be larger than 1");
	}
	else {
		x->cloudsize_new = arg;
		x->resize_request = true;
	}
}


/************************************************************************************************************************/
/* THE ACTUAL RESIZE METHOD                                                                                             */
/************************************************************************************************************************/
t_bool cmlivecloud_resize(t_cmlivecloud *x) {
	int i;
	
	for (i = 0; i < x->cloudsize; i++) {
		sysmem_freeptr(x->cloud[i].left);
		sysmem_freeptr(x->cloud[i].right);
	}
	sysmem_freeptr(x->cloud);
	
	x->cloudsize = x->cloudsize_new;
	
	// ALLOCATE MEMORY FOR THE GRAINMEM ARRAY
	x->cloud = (cm_cloud *)sysmem_newptrclear((x->cloudsize) * sizeof(cm_cloud));
	if (x->cloud == NULL) {
		object_error((t_object *)x, "out of memory");
		x->resize_verify = false;
		return false;
	}
	
	// ALLOCATE MEMORY FOR THE GRAIN ARRAY IN EACH MEMBER OF THE GRAINMEM STRUCT
	for (i = 0; i < x->cloudsize; i++) {
		x->cloud[i].left = (double *)sysmem_newptrclear(((MAX_GRAINLENGTH * x->m_sr) * MAX_PITCH) * sizeof(double));
		if (x->cloud[i].left == NULL) {
			object_error((t_object *)x, "out of memory");
			x->resize_verify = false;
			return false;
		}
		x->cloud[i].right = (double *)sysmem_newptrclear(((MAX_GRAINLENGTH * x->m_sr) * MAX_PITCH) * sizeof(double));
		if (x->cloud[i].right == NULL) {
			object_error((t_object *)x, "out of memory");
			x->resize_verify = false;
			return false;
		}
	}
	
	return true;
}

/************************************************************************************************************************/
/* THE RECORD METHOD                                                                                                    */
/************************************************************************************************************************/
void cmlivecloud_record(t_cmlivecloud *x, t_symbol *s, long ac, t_atom *av) {
	long arg;
	arg = atom_getlong(av);
	if (arg <= 0) { // smaller or equal to zero
		x->record = 0;
//		object_post((t_object*)x, "record off");
	}
	else { // any non-zero value sets x->record to true
		x->record = 1;
		x->recordflag = 1;
//		object_post((t_object*)x, "record on");
	}
}


/************************************************************************************************************************/
/* THE BANG METHOD                                                                                                      */
/************************************************************************************************************************/
void cmlivecloud_bang(t_cmlivecloud *x) {
	x->bang_trigger = 1;
}


/************************************************************************************************************************/
/* THE WINDOW INTERPOLATION ATTRIBUTE SET METHOD                                                                        */
/************************************************************************************************************************/
t_max_err cmlivecloud_winterp_set(t_cmlivecloud *x, t_object *attr, long ac, t_atom *av) {
	if (ac && av) {
		x->attr_winterp = atom_getlong(av)? 1 : 0;
	}
	return MAX_ERR_NONE;
}


/************************************************************************************************************************/
/* THE SAMPLE INTERPOLATION ATTRIBUTE SET METHOD                                                                        */
/************************************************************************************************************************/
t_max_err cmlivecloud_sinterp_set(t_cmlivecloud *x, t_object *attr, long ac, t_atom *av) {
	if (ac && av) {
		x->attr_sinterp = atom_getlong(av)? 1 : 0;
	}
	return MAX_ERR_NONE;
}


/************************************************************************************************************************/
/* THE ZERO CROSSING ATTRIBUTE SET METHOD                                                                               */
/************************************************************************************************************************/
t_max_err cmlivecloud_zero_set(t_cmlivecloud *x, t_object *attr, long ac, t_atom *av) {
	if (ac && av) {
		x->attr_zero = atom_getlong(av)? 1 : 0;
	}
	return MAX_ERR_NONE;
}


/************************************************************************************************************************/
/* CUSTOM FUNCTIONS																										*/
/************************************************************************************************************************/
// constant power stereo function
void cm_panning(cm_panstruct *panstruct, double *pos, t_cmlivecloud *x) {
	panstruct->left = x->root2ovr2 * (cos((*pos * x->piovr2) * 0.5) - sin((*pos * x->piovr2) * 0.5));
	panstruct->right = x->root2ovr2 * (cos((*pos * x->piovr2) * 0.5) + sin((*pos * x->piovr2) * 0.5));
	return;
}
// RANDOM NUMBER GENERATOR (USE POINTERS FOR MORE EFFICIENCY)
double cm_random(double *min, double *max) {
#ifdef MAC_VERSION
	return *min + ((*max - *min) * (((double)arc4random_uniform(RANDMAX)) / (double)RANDMAX));
#endif
#ifdef WIN_VERSION
	return *min + ((*max - *min) * ((double)(rand() % RANDMAX) / (double)RANDMAX));
#endif
}
// LINEAR INTERPOLATION FUNCTION
double cm_lininterp(double distance, float *buffer, t_atom_long b_channelcount, t_atom_long b_framecount, short channel) {
	long index = (long)distance; // get truncated index
	long next = index + 1;
	if (next > b_framecount) {
		next = 0;
	}
	distance -= (long)distance; // calculate fraction value for interpolation
	return buffer[index * b_channelcount + channel] + distance * (buffer[next * b_channelcount + channel] - buffer[index * b_channelcount + channel]);
}

double cm_lininterpring(double distance, long index, long next, double *ringbuffer) {

	return ringbuffer[index] + distance * (ringbuffer[next] - ringbuffer[index]);

}
