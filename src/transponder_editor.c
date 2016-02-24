#include <stdlib.h>
#include "transponder_editor.h"


#define TRANSPONDER_FREQUENCY_LENGTH 10
#define TRANSPONDER_NAME_LENGTH 20
#define TRANSPONDER_PHASE_LENGTH 10
#define TRANSPONDER_DOW_LENGTH 10

#define NUM_FIELDS_IN_ENTRY (NUM_TRANSPONDER_SPECIFIERS*3 + 2)

#define FIELD_HEIGHT 1
#define SPACING 2

FIELD *create_field(int field_height, int field_width, int field_row, int field_col) {
	FIELD *ret_field = new_field(field_height, field_width, field_row, field_col, 0, 0);
	set_field_back(ret_field, TRANSPONDER_ENTRY_DEFAULT_STYLE);
	return ret_field;
}

bool transponder_line_status(struct transponder_line *transponder_line)
{
	return (field_status(transponder_line->transponder_name) == TRUE);
}

void transponder_line_set_visible(struct transponder_line *transponder_line) {
	field_opts_on(transponder_line->transponder_name, O_VISIBLE);

	field_opts_on(transponder_line->uplink[0], O_VISIBLE);
	field_opts_on(transponder_line->uplink[1], O_VISIBLE);

	field_opts_on(transponder_line->downlink[0], O_VISIBLE);
	field_opts_on(transponder_line->downlink[1], O_VISIBLE);

	field_opts_on(transponder_line->phase[0], O_VISIBLE);
	field_opts_on(transponder_line->phase[1], O_VISIBLE);

	field_opts_on(transponder_line->dayofweek, O_VISIBLE);
}

struct transponder_line* transponder_editor_line_create(int row)
{
	struct transponder_line *ret_line = (struct transponder_line*)malloc(sizeof(struct transponder_line));

	ret_line->transponder_name = create_field(FIELD_HEIGHT, TRANSPONDER_NAME_LENGTH, row, 1);

	ret_line->uplink[0] = create_field(FIELD_HEIGHT, TRANSPONDER_FREQUENCY_LENGTH, row, TRANSPONDER_NAME_LENGTH+1+SPACING);
	ret_line->uplink[1] = create_field(FIELD_HEIGHT, TRANSPONDER_FREQUENCY_LENGTH, row+2, TRANSPONDER_NAME_LENGTH+1+SPACING);

	ret_line->downlink[0] = create_field(FIELD_HEIGHT, TRANSPONDER_FREQUENCY_LENGTH, row, TRANSPONDER_NAME_LENGTH+TRANSPONDER_FREQUENCY_LENGTH+1+2*SPACING);
	ret_line->downlink[1] = create_field(FIELD_HEIGHT, TRANSPONDER_FREQUENCY_LENGTH, row+2, TRANSPONDER_NAME_LENGTH+TRANSPONDER_FREQUENCY_LENGTH+1+2*SPACING);

	ret_line->phase[0] = create_field(FIELD_HEIGHT, TRANSPONDER_FREQUENCY_LENGTH, row, TRANSPONDER_NAME_LENGTH+TRANSPONDER_FREQUENCY_LENGTH*2+1+3*SPACING);
	ret_line->phase[1] = create_field(FIELD_HEIGHT, TRANSPONDER_FREQUENCY_LENGTH, row+2, TRANSPONDER_NAME_LENGTH+TRANSPONDER_FREQUENCY_LENGTH*2+1+3*SPACING);

	ret_line->dayofweek = create_field(FIELD_HEIGHT, TRANSPONDER_DOW_LENGTH, row, TRANSPONDER_NAME_LENGTH+TRANSPONDER_FREQUENCY_LENGTH*2+TRANSPONDER_PHASE_LENGTH+4*SPACING);

	return ret_line;
}

void transponder_editor_entry_fill(struct transponder_entry *entry, struct sat_db_entry *db_entry)
{
	char temp[MAX_NUM_CHARS];
	for (int i=0; i < db_entry->num_transponders; i++) {
		set_field_buffer(entry->transponders[i]->transponder_name, 0, db_entry->transponder_name[i]);

		snprintf(temp, MAX_NUM_CHARS, "%f", db_entry->uplink_start[i]);
		set_field_buffer(entry->transponders[i]->uplink[0], 0, temp);

		snprintf(temp, MAX_NUM_CHARS, "%f", db_entry->uplink_end[i]);
		set_field_buffer(entry->transponders[i]->uplink[1], 0, temp);

		snprintf(temp, MAX_NUM_CHARS, "%f", db_entry->downlink_start[i]);
		set_field_buffer(entry->transponders[i]->downlink[0], 0, temp);

		snprintf(temp, MAX_NUM_CHARS, "%f", db_entry->downlink_end[i]);
		set_field_buffer(entry->transponders[i]->downlink[1], 0, temp);

		snprintf(temp, MAX_NUM_CHARS, "%c", db_entry->dayofweek[i]);
		set_field_buffer(entry->transponders[i]->dayofweek, 0, temp);

		snprintf(temp, MAX_NUM_CHARS, "%d", db_entry->phase_start[i]);
		set_field_buffer(entry->transponders[i]->phase[0], 0, temp);
		snprintf(temp, MAX_NUM_CHARS, "%d", db_entry->phase_end[i]);
		set_field_buffer(entry->transponders[i]->phase[1], 0, temp);
	}
}

struct transponder_entry* transponder_editor_entry_create(WINDOW *window, struct sat_db_entry *db_entry)
{
	struct transponder_entry *new_entry = (struct transponder_entry*)malloc(sizeof(struct transponder_entry));

	//create FIELDs for each editable field
	int row = 1;
	for (int i=0; i < MAX_NUM_TRANSPONDERS; i++) {
		new_entry->transponders[i] = transponder_editor_line_create(row);
		row += 4;
	}

	//create horrible FIELD array for input into the form
	FIELD **fields = calloc(NUM_FIELDS_IN_ENTRY*MAX_NUM_TRANSPONDERS + 1, sizeof(FIELD*));
	for (int i=0; i < MAX_NUM_TRANSPONDERS; i++) {
		int field_index = i*NUM_FIELDS_IN_ENTRY;
		fields[field_index] = new_entry->transponders[i]->transponder_name;
		fields[field_index + 1] = new_entry->transponders[i]->uplink[0];
		fields[field_index + 2] = new_entry->transponders[i]->downlink[0];
		fields[field_index + 3] = new_entry->transponders[i]->phase[0];
		fields[field_index + 4] = new_entry->transponders[i]->dayofweek;
		fields[field_index + 5] = new_entry->transponders[i]->uplink[1];
		fields[field_index + 6] = new_entry->transponders[i]->downlink[1];
		fields[field_index + 7] = new_entry->transponders[i]->phase[1];

		if (i > db_entry->num_transponders) {
			for (int j=field_index; j < field_index+8; j++) {
				field_opts_off(fields[j], O_VISIBLE);
			}
		}
	}
	new_entry->num_editable_transponders = db_entry->num_transponders+1;
	fields[NUM_FIELDS_IN_ENTRY*MAX_NUM_TRANSPONDERS] = NULL;

	new_entry->form = new_form(fields);

	int rows, cols;
	scale_form(new_entry->form, &rows, &cols);
	wresize(window, rows+4, cols+4);

	keypad(window, TRUE);
	wattrset(window, COLOR_PAIR(4));
	box(window, 0, 0);

	/* Set main window and sub window */
	set_form_win(new_entry->form, window);
	set_form_sub(new_entry->form, derwin(window, rows, cols, 2, 2));

	post_form(new_entry->form);

	mvwprintw(window, 1, 3, "Transponder name      Uplink      Downlink    Phase      Day of week");

	refresh();
	form_driver(new_entry->form, REQ_VALIDATION);

	new_entry->prev_selected_field = current_field(new_entry->form);
	set_field_back(new_entry->prev_selected_field, TRANSPONDER_ACTIVE_FIELDSTYLE);

	transponder_editor_entry_fill(new_entry, db_entry);
	return new_entry;
}

void transponder_entry_handle(struct transponder_entry *transponder_entry, int c)
{
	//handle keyboard
	switch (c) {
		case KEY_UP:
			form_driver(transponder_entry->form, REQ_UP_FIELD);
			break;
		case KEY_DOWN:
			form_driver(transponder_entry->form, REQ_DOWN_FIELD);
			break;
		case KEY_LEFT:
			form_driver(transponder_entry->form, REQ_LEFT_FIELD);
			break;
		case KEY_RIGHT:
			form_driver(transponder_entry->form, REQ_RIGHT_FIELD);
			break;
		case KEY_BACKSPACE:
			form_driver(transponder_entry->form, REQ_DEL_PREV);
		default:
			form_driver(transponder_entry->form, c);
			form_driver(transponder_entry->form, REQ_VALIDATION); //update buffer with field contents
			break;
	}

	//switch background color of currently marked field, reset previous field
	FIELD *curr_field = current_field(transponder_entry->form);
	if (curr_field != transponder_entry->prev_selected_field) {
		set_field_back(transponder_entry->prev_selected_field, TRANSPONDER_ENTRY_DEFAULT_STYLE);
		set_field_back(curr_field, TRANSPONDER_ACTIVE_FIELDSTYLE);
		transponder_entry->prev_selected_field = curr_field;
	}

	//add a new transponder editor field if last entry has been edited
	if ((transponder_entry->num_editable_transponders < MAX_NUM_TRANSPONDERS) && (transponder_line_status(transponder_entry->transponders[transponder_entry->num_editable_transponders-1]))) {
		transponder_line_set_visible(transponder_entry->transponders[transponder_entry->num_editable_transponders]);
		transponder_entry->num_editable_transponders++;
	}
}


void transponder_editor_entry_clear(struct transponder_entry *entry) {

}

void transponder_db_entry_from_editor(struct sat_db_entry *db_entry, struct transponder_entry *entry)
{
}