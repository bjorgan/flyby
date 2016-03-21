#include <predict/predict.h>
#include "defines.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <curses.h>
#include <stdlib.h>
#include "tle_db.h"
#include "multitrack.h"

int MultiColours(double range, double elevation);

//header (Satellite Azim Elev ...) color style
#define HEADER_STYLE COLOR_PAIR(2)|A_REVERSE

//color attributes of selected entry in satellite listing
#define MULTITRACK_SELECTED_ATTRIBUTE (COLOR_PAIR(6)|A_REVERSE)

//marker of menu item
#define MULTITRACK_SELECTED_MARKER '-'

/**
 * Create entry in multitrack satellite listing.
 *
 * \param name Satellite name
 * \param orbital_elements Orbital elements of satellite, created from TLE
 * \return Multitrack entry
 **/
multitrack_entry_t *multitrack_create_entry(const char *name, predict_orbital_elements_t *orbital_elements);

/**
 * Print scrollbar for satellite listing.
 *
 * \param listing Satellite listing
 **/
void multitrack_print_scrollbar(multitrack_listing_t *listing);
/**
 * Display entry in satellite listing.
 *
 * \param window Window to display entry in
 * \param row Row
 * \param col Column
 * \param entry Satellite entry
 **/
void multitrack_display_entry(WINDOW *window, int row, int col, multitrack_entry_t *entry);

/**
 * Update display strings and status in satellite entry.
 *
 * \param qth QTH coordinates
 * \param entry Multitrack entry
 * \param time Time at which satellite status should be calculated
 **/
void multitrack_update_entry(predict_observer_t *qth, multitrack_entry_t *entry, predict_julian_date_t time);

/**
 * Sort satellite listing in different categories: Currently above horizon, below horizon but will rise, will never rise above horizon, decayed satellites. The satellites below the horizon are sorted internally according to AOS times.
 *
 * \param listing Satellite listing
 **/
void multitrack_sort_listing(multitrack_listing_t *listing);

/**
 * Create option selector submenu.
 *
 * \return Option selector
 **/
multitrack_option_selector_t* multitrack_option_selector_create();

/**
 * Destroy option selector.
 *
 * \param option_selector Option selector to free
 **/
void multitrack_option_selector_destroy(multitrack_option_selector_t **option_selector);

/**
 * Hide option selector.
 *
 * \param option_selector Option selector
 **/
void multitrack_option_selector_hide(multitrack_option_selector_t *option_selector);

/**
 * Show option selector.
 *
 * \param option_selector Option selector
 **/
void multitrack_option_selector_show(multitrack_option_selector_t *option_selector);

/**
 * Check whether option selector is visible.
 *
 * \return True if option selector is visible, false otherwise
 **/
bool multitrack_option_selector_visible(multitrack_option_selector_t *option_selector);

/**
 * Display option selector on specified row number in the standard screen.
 *
 * \param row Row to place submenu window
 * \param option_selector Option selector
 **/
void multitrack_option_selector_display(int row, multitrack_option_selector_t *option_selector);

/**
 * Handle input characters to option selector. KEY_UP/KEY_DOWN scrolls menu, KEY_LEFT/q/ESC hides the menu,
 * while ENTER/KEY_RIGHT also sets the `option_selected` flag in addition to hiding the menu.
 *
 * \param option_selector Option selector
 * \param input_key Input key
 * \return True if input key was handled, false otherwise
 **/
bool multitrack_option_selector_handle(multitrack_option_selector_t *option_selector, int input_key);

multitrack_searcher_t *multitrack_searcher_create(int row, int col);

void multitrack_searcher_show(multitrack_searcher_t *search_field);

void multitrack_searcher_display(multitrack_searcher_t *search_field);

void multitrack_searcher_hide(multitrack_searcher_t *search_field);

void multitrack_searcher_destroy(multitrack_searcher_t **search_field);

bool multitrack_searcher_visible(multitrack_searcher_t *search_field);

bool multitrack_searcher_handle(multitrack_searcher_t *search_field, int input_key);

void multitrack_searcher_match(multitrack_searcher_t *search_field, bool match);

char *multitrack_searcher_string(multitrack_searcher_t *search_field);

void multitrack_listing_next_match(multitrack_listing_t *listing);
void multitrack_searcher_add_match(multitrack_searcher_t *search_field, int index);
void multitrack_searcher_clear_matches(multitrack_searcher_t *search_field);

multitrack_entry_t *multitrack_create_entry(const char *name, predict_orbital_elements_t *orbital_elements)
{
	multitrack_entry_t *entry = (multitrack_entry_t*)malloc(sizeof(multitrack_entry_t));
	entry->orbital_elements = orbital_elements;
	entry->name = strdup(name);
	entry->next_aos = 0;
	entry->next_los = 0;
	entry->above_horizon = 0;
	entry->geostationary = 0;
	entry->never_visible = 0;
	entry->decayed = 0;
	return entry;
}

multitrack_listing_t* multitrack_create_listing(WINDOW *window, predict_observer_t *observer, struct tle_db *tle_db)
{
	multitrack_listing_t *listing = (multitrack_listing_t*)malloc(sizeof(multitrack_listing_t));
	listing->window = window;

	int window_height, window_width;
	getmaxyx(window, window_height, window_width);
	listing->window_height = window_height;
	listing->window_width = window_width;

	listing->num_entries = 0;
	listing->entries = NULL;
	listing->tle_db_mapping = NULL;
	listing->sorted_index = NULL;

	//prepare window for header printing
	int window_row = getbegy(listing->window);
	listing->header_window = newwin(1, window_width+11, window_row-2, 0);
	listing->window_row = window_row;

	listing->qth = observer;
	listing->displayed_entries_per_page = window_height-MULTITRACK_PRINT_OFFSET;

	multitrack_refresh_tles(listing, tle_db);

	listing->option_selector = multitrack_option_selector_create();

	int search_row = window_row + window_height + 1;
	int search_col = 0;
	listing->search_field = multitrack_searcher_create(search_row, search_col);
	return listing;
}

void multitrack_search_listing(multitrack_listing_t *listing)
{
	multitrack_searcher_clear_matches(listing->search_field);
	char *expression = multitrack_searcher_string(listing->search_field);
	if (strlen(expression) == 0) {
		free(expression);
		return;
	}
	for (int i=0; i < listing->num_entries; i++) {
		if (strstr(listing->entries[listing->sorted_index[i]]->name, expression) != NULL) {
			multitrack_searcher_add_match(listing->search_field, i);
		}
	}
	multitrack_listing_next_match(listing);
	free(expression);
}

void multitrack_searcher_clear_matches(multitrack_searcher_t *search_field)
{
	multitrack_searcher_match(search_field, false);
	search_field->match_num = -1;

	if (search_field->matches != NULL) {
		free(search_field->matches);
		search_field->matches = NULL;
	}
	search_field->num_matches = 0;
	search_field->available_match_size = 0;
}

void multitrack_searcher_add_match(multitrack_searcher_t *search_field, int index)
{
	multitrack_searcher_match(search_field, true);
	if (search_field->matches == NULL) {
		search_field->available_match_size = 2;
		search_field->matches = (int*)malloc(sizeof(int)*search_field->available_match_size);
	}

	if (search_field->num_matches == search_field->available_match_size) {
		search_field->available_match_size = search_field->available_match_size*2;
		search_field->matches = (int*)realloc(search_field->matches, sizeof(int)*search_field->available_match_size);
	}

	search_field->matches[search_field->num_matches++] = index;
}

void multitrack_listing_next_match(multitrack_listing_t *listing)
{
	multitrack_searcher_t *search_field = listing->search_field;
	if (search_field->num_matches > 0) {
		search_field->match_num = (search_field->match_num + 1) % (search_field->num_matches);
		listing->selected_entry_index = search_field->matches[search_field->match_num];
	}
}

void multitrack_free_entry(multitrack_entry_t **entry)
{
	predict_destroy_orbital_elements((*entry)->orbital_elements);
	free((*entry)->name);
	free(*entry);
	*entry = NULL;
}

void multitrack_free_entries(multitrack_listing_t *listing)
{
	if (listing->entries != NULL) {
		for (int i=0; i < listing->num_entries; i++) {
			multitrack_free_entry(&(listing->entries[i]));
		}
		free(listing->entries);
		listing->entries = NULL;
	}
	if (listing->tle_db_mapping != NULL) {
		free(listing->tle_db_mapping);
		listing->tle_db_mapping = NULL;
	}
	if (listing->sorted_index != NULL) {
		free(listing->sorted_index);
		listing->sorted_index = NULL;
	}
	listing->num_entries = 0;
}

void multitrack_refresh_tles(multitrack_listing_t *listing, struct tle_db *tle_db)
{
	listing->not_displayed = true;
	multitrack_free_entries(listing);

	int num_enabled_tles = 0;
	for (int i=0; i < tle_db->num_tles; i++) {
		if (tle_db_entry_enabled(tle_db, i)) {
			num_enabled_tles++;
		}
	}

	listing->num_entries = num_enabled_tles;

	if (listing->num_entries > 0) {
		listing->entries = (multitrack_entry_t**)malloc(sizeof(multitrack_entry_t*)*num_enabled_tles);
		listing->tle_db_mapping = (int*)calloc(num_enabled_tles, sizeof(int));
		listing->sorted_index = (int*)calloc(tle_db->num_tles, sizeof(int));

		int j=0;
		for (int i=0; i < tle_db->num_tles; i++) {
			if (tle_db_entry_enabled(tle_db, i)) {
				predict_orbital_elements_t *orbital_elements = tle_db_entry_to_orbital_elements(tle_db, i);
				listing->entries[j] = multitrack_create_entry(tle_db_entry_name(tle_db, i), orbital_elements);
				listing->tle_db_mapping[j] = i;
				listing->sorted_index[j] = j;
				j++;
			}
		}
	}

	listing->selected_entry_index = 0;
	listing->top_index = 0;
	listing->bottom_index = listing->top_index + listing->displayed_entries_per_page - 1;

	listing->num_above_horizon = 0;
	listing->num_below_horizon = 0;
	listing->num_decayed = 0;
	listing->num_nevervisible = 0;

}

void multitrack_update_entry(predict_observer_t *qth, multitrack_entry_t *entry, predict_julian_date_t time)
{
	entry->geostationary = false;

	struct predict_observation obs;
	struct predict_orbit orbit;
	predict_orbit(entry->orbital_elements, &orbit, time);
	predict_observe_orbit(qth, &orbit, &obs);

	//sun status
	char sunstat;
	if (!orbit.eclipsed) {
		if (obs.visible) {
			sunstat='V';
		} else {
			sunstat='D';
		}
	} else {
		sunstat='N';
	}

	//satellite approaching status
	char rangestat;
	if (fabs(obs.range_rate) < 0.1) {
		rangestat = '=';
	} else if (obs.range_rate < 0.0) {
		rangestat = '/';
	} else if (obs.range_rate > 0.0) {
		rangestat = '\\';
	}

	//set text formatting attributes according to satellite state, set AOS/LOS string
	bool can_predict = !predict_is_geostationary(entry->orbital_elements) && predict_aos_happens(entry->orbital_elements, qth->latitude) && !(orbit.decayed);
	char aos_los[MAX_NUM_CHARS] = {0};
	if (obs.elevation >= 0) {
		//different colours according to range and elevation
		entry->display_attributes = MultiColours(obs.range, obs.elevation*180/M_PI);

		if (predict_is_geostationary(entry->orbital_elements)){
			sprintf(aos_los, "*GeoS*");
			entry->geostationary = true;
		} else {
			time_t epoch = predict_from_julian(entry->next_los - time);
			struct tm timeval;
			gmtime_r(&epoch, &timeval);
			if ((entry->next_los - time) > 1.0) {
				int num_days = (entry->next_los - time);
				snprintf(aos_los, MAX_NUM_CHARS, "%d days", num_days);
			} else if (timeval.tm_hour > 0) {
				strftime(aos_los, MAX_NUM_CHARS, "%H:%M:%S", &timeval);
			} else {
				strftime(aos_los, MAX_NUM_CHARS, "%M:%S", &timeval);
			}

		}
	} else if ((obs.elevation < 0) && can_predict) {
		if ((entry->next_aos-time) < 0.00694) {
			//satellite is close, set bold
			entry->display_attributes = COLOR_PAIR(2);
			time_t epoch = predict_from_julian(entry->next_aos - time);
			strftime(aos_los, MAX_NUM_CHARS, "%M:%S", gmtime(&epoch)); //minutes and seconds left until AOS
		} else {
			//satellite is far, set normal coloring
			entry->display_attributes = COLOR_PAIR(4);
			time_t aoslos_epoch = predict_from_julian(entry->next_aos);
			time_t curr_epoch = predict_from_julian(time);
			struct tm aostime, currtime;
			gmtime_r(&aoslos_epoch, &aostime);
			gmtime_r(&curr_epoch, &currtime);
			aostime.tm_yday = aostime.tm_yday - currtime.tm_yday;
			char temp[MAX_NUM_CHARS];
			strftime(temp, MAX_NUM_CHARS, "%H:%MZ", &aostime);
			if (aostime.tm_yday == 0) {
				strncpy(aos_los, temp, MAX_NUM_CHARS);
			} else {
				snprintf(aos_los, MAX_NUM_CHARS, "+%dd %s", aostime.tm_yday, temp);
			}
		}
	} else if (!can_predict) {
		entry->display_attributes = COLOR_PAIR(3);
		sprintf(aos_los, "*GeoS-NoAOS*");
	}

	char abs_pos_string[MAX_NUM_CHARS] = {0};
	sprintf(abs_pos_string, "%3.0f  %3.0f", orbit.latitude*180.0/M_PI, orbit.longitude*180.0/M_PI);

	/* Calculate Next Event (AOS/LOS) Times */
	if (can_predict && (time > entry->next_los) && (obs.elevation > 0)) {
		entry->next_los= predict_next_los(qth, entry->orbital_elements, time);
	}

	if (can_predict && (time > entry->next_aos)) {
		if (obs.elevation < 0) {
			entry->next_aos = predict_next_aos(qth, entry->orbital_elements, time);
		}
	}

	//altitude and range in km/miles
	double disp_altitude = orbit.altitude;
	double disp_range = obs.range;

	//set string to display
	char disp_string[MAX_NUM_CHARS];
	sprintf(disp_string, " %-10.8s%5.1f  %5.1f %8s%6.0f %6.0f %c %c %12s ", entry->name, obs.azimuth*180.0/M_PI, obs.elevation*180.0/M_PI, abs_pos_string, disp_altitude, disp_range, sunstat, rangestat, aos_los);

	//overwrite everything if orbit was decayed
	if (orbit.decayed) {
		entry->display_attributes = COLOR_PAIR(2);
		sprintf(disp_string, " %-10s ----------------     Decayed       --------------- ", entry->name);
	}

	memcpy(entry->display_string, disp_string, sizeof(char)*MAX_NUM_CHARS);

	entry->above_horizon = obs.elevation > 0;
	entry->decayed = orbit.decayed;

	entry->never_visible = !predict_aos_happens(entry->orbital_elements, qth->latitude) || (predict_is_geostationary(entry->orbital_elements) && (obs.elevation <= 0.0));
}

void multitrack_update_listing(multitrack_listing_t *listing, predict_julian_date_t time)
{
	for (int i=0; i < listing->num_entries; i++) {
		if (listing->not_displayed) {
			//display progress information when this is the first time entries are displayed
			wattrset(listing->window, COLOR_PAIR(1));
			mvwprintw(listing->window, 0, 1, "Preparing entry %d of %d\n", i, listing->num_entries);
			wrefresh(listing->window);
		}
		multitrack_entry_t *entry = listing->entries[i];
		multitrack_update_entry(listing->qth, entry, time);
	}

	if (!multitrack_option_selector_visible(listing->option_selector)) {
		multitrack_sort_listing(listing); //freeze sorting when option selector is hovering over a satellite
	}

	listing->not_displayed = false;
}

void multitrack_sort_listing(multitrack_listing_t *listing)
{
	int num_orbits = listing->num_entries;

	//those with elevation > 0 at the top
	int above_horizon_counter = 0;
	for (int i=0; i < num_orbits; i++){
		if (listing->entries[i]->above_horizon && !(listing->entries[i]->decayed)) {
			listing->sorted_index[above_horizon_counter] = i;
			above_horizon_counter++;
		}
	}
	listing->num_above_horizon = above_horizon_counter;

	//satellites that will eventually rise above the horizon
	int below_horizon_counter = 0;
	for (int i=0; i < num_orbits; i++){
		if (!(listing->entries[i]->above_horizon) && !(listing->entries[i]->never_visible) && !(listing->entries[i]->decayed)) {
			listing->sorted_index[below_horizon_counter + above_horizon_counter] = i;
			below_horizon_counter++;
		}
	}
	listing->num_below_horizon = below_horizon_counter;

	//satellites that will never be visible, with decayed orbits last
	int nevervisible_counter = 0;
	int decayed_counter = 0;
	for (int i=0; i < num_orbits; i++){
		if (listing->entries[i]->never_visible && !(listing->entries[i]->decayed)) {
			listing->sorted_index[below_horizon_counter + above_horizon_counter + nevervisible_counter] = i;
			nevervisible_counter++;
		} else if (listing->entries[i]->decayed) {
			listing->sorted_index[num_orbits - 1 - decayed_counter] = i;
			decayed_counter++;
		}
	}
	listing->num_nevervisible = nevervisible_counter;
	listing->num_decayed = decayed_counter;

	//sort internally according to AOS/LOS
	for (int i=0; i < above_horizon_counter + below_horizon_counter; i++) {
		for (int j=0; j < above_horizon_counter + below_horizon_counter - 1; j++){
			if (listing->entries[listing->sorted_index[j]]->next_aos > listing->entries[listing->sorted_index[j+1]]->next_aos) {
				int x = listing->sorted_index[j];
				listing->sorted_index[j] = listing->sorted_index[j+1];
				listing->sorted_index[j+1] = x;
			}
		}
	}
}

void multitrack_display_entry(WINDOW *window, int row, int col, multitrack_entry_t *entry)
{
	wattrset(window, entry->display_attributes);
	mvwprintw(window, row, col, "%s", entry->display_string);
}

void multitrack_print_scrollbar(multitrack_listing_t *listing)
{
	int scrollarea_offset = MULTITRACK_PRINT_OFFSET;
	int scrollarea_height = listing->window_height-MULTITRACK_PRINT_OFFSET;
	int scrollbar_height = ((listing->displayed_entries_per_page*1.0)/(listing->num_entries*1.0))*scrollarea_height;

	//print scrollarea
	for (int i=0; i < scrollarea_height; i++) {
		int row = i+scrollarea_offset;
		wattrset(listing->window, COLOR_PAIR(8));
		mvwprintw(listing->window, row, listing->window_width-2, "  ");
	}

	//print scrollbar
	int scrollbar_placement = (listing->top_index*1.0/(listing->num_entries - listing->displayed_entries_per_page))*(scrollarea_height - scrollbar_height);
	for (int i=scrollbar_placement; i < scrollbar_height+scrollbar_placement; i++) {
		int row = i+scrollarea_offset;
		wattrset(listing->window, COLOR_PAIR(8)|A_REVERSE);
		mvwprintw(listing->window, row, listing->window_width-2, "  ");
	}
}

void multitrack_display_listing(multitrack_listing_t *listing)
{
	//show header
	wbkgd(listing->header_window, HEADER_STYLE);
	wattrset(listing->header_window, HEADER_STYLE);
	mvwprintw(listing->header_window, 0, 0, "  Satellite  Azim   Elev Lat Long   Alt   Range     Next AOS/LOS   ");

	//show entries
	if (listing->num_entries > 0) {
		int selected_index = listing->sorted_index[listing->selected_entry_index];
		listing->entries[selected_index]->display_attributes = MULTITRACK_SELECTED_ATTRIBUTE;
		listing->entries[selected_index]->display_string[0] = MULTITRACK_SELECTED_MARKER;

		int line = MULTITRACK_PRINT_OFFSET;
		int col = 1;

		for (int i=listing->top_index; ((i <= listing->bottom_index) && (i < listing->num_entries)); i++) {
			multitrack_display_entry(listing->window, line++, col, listing->entries[listing->sorted_index[i]]);
		}

		if (listing->num_entries > listing->displayed_entries_per_page) {
			multitrack_print_scrollbar(listing);
		}
	} else {
		wattrset(listing->window, COLOR_PAIR(1));
		mvwprintw(listing->window, 5, 2, "Satellite list is empty. Are any satellites enabled?");
		mvwprintw(listing->window, 6, 2, "(Press 'W' to enable satellites)");
	}
	wrefresh(listing->window);
	wrefresh(listing->header_window);

	//refresh search field
	multitrack_searcher_display(listing->search_field);

	//refresh option selector
	int option_selector_row = multitrack_selected_window_row(listing) + listing->window_row + MULTITRACK_PRINT_OFFSET + 1;
	multitrack_option_selector_display(option_selector_row, listing->option_selector);
}

bool multitrack_handle_listing(multitrack_listing_t *listing, int input_key)
{
	if (multitrack_option_selector_visible(listing->option_selector)) {
		return multitrack_option_selector_handle(listing->option_selector, input_key);
	} else {
		bool handled = false;
		switch (input_key) {
			case KEY_UP:
				listing->selected_entry_index--;
				handled = true;
				break;
			case KEY_DOWN:
				listing->selected_entry_index++;
				handled = true;
				break;
			case KEY_PPAGE:
				listing->selected_entry_index -= listing->displayed_entries_per_page;
				handled = true;
				break;
			case KEY_NPAGE:
				listing->selected_entry_index += listing->displayed_entries_per_page;
				handled = true;
				break;
			case 10:
			case KEY_RIGHT:
				if (listing->num_entries > 0) {
					multitrack_option_selector_show(listing->option_selector);
				}
				handled = true;
				break;
			case '/':
				multitrack_searcher_show(listing->search_field);
				handled = true;
				break;
			case 27:
				multitrack_searcher_hide(listing->search_field);
				handled = true;
				break;
			case 't':
			case 'T':
				if (!multitrack_searcher_visible(listing->search_field)) {
					//select single track mode in option selector and
					//signal that an option has been selected
					set_menu_pattern(listing->option_selector->menu, "Track satellite");
					listing->option_selector->option_selected = true;
					handled = true;
					break;
				}
			case KEY_F(3):
				if (multitrack_searcher_visible(listing->search_field)) {
					multitrack_listing_next_match(listing);
				} else {
					multitrack_searcher_show(listing->search_field);
				}
				handled = true;
				break;
			default:
				if (multitrack_searcher_visible(listing->search_field)) {
					multitrack_searcher_handle(listing->search_field, input_key);
					multitrack_search_listing(listing);
					handled = true;
				}
			break;
		}

		//adjust index according to limits
		if (listing->selected_entry_index < 0) {
			listing->selected_entry_index = 0;
		}

		if (listing->selected_entry_index >= listing->num_entries) {
			listing->selected_entry_index = listing->num_entries-1;
		}

		//check for scroll event
		if (listing->selected_entry_index > listing->bottom_index) {
			int diff = listing->selected_entry_index - listing->bottom_index;
			listing->bottom_index += diff;
			listing->top_index += diff;
		}
		if (listing->selected_entry_index < listing->top_index) {
			int diff = listing->top_index - listing->selected_entry_index;
			listing->bottom_index -= diff;
			listing->top_index -= diff;
		}
		return handled;
	}
}

int multitrack_selected_entry(multitrack_listing_t *listing)
{
	int index = listing->sorted_index[listing->selected_entry_index];
	return listing->tle_db_mapping[index];
}

int multitrack_selected_window_row(multitrack_listing_t *listing)
{
	return listing->selected_entry_index - listing->top_index;
}

void multitrack_destroy_listing(multitrack_listing_t **listing)
{
	multitrack_free_entries(*listing);
	multitrack_option_selector_destroy(&((*listing)->option_selector));
	multitrack_searcher_destroy(&((*listing)->search_field));
	delwin((*listing)->header_window);
	free(*listing);
	*listing = NULL;
}

#define NUM_OPTIONS 7

void multitrack_option_selector_destroy(multitrack_option_selector_t **option_selector)
{
	unpost_menu((*option_selector)->menu);
	free_menu((*option_selector)->menu);
	delwin((*option_selector)->sub_window);
	delwin((*option_selector)->window);
	for (int i=0; i < NUM_OPTIONS; i++) {
		free_item((*option_selector)->items[i]);
	}
	free((*option_selector)->items);
	free((*option_selector)->item_types);
	free((*option_selector));
	*option_selector = NULL;
}

multitrack_option_selector_t* multitrack_option_selector_create()
{
	multitrack_option_selector_t *option_selector = (multitrack_option_selector_t*)malloc(sizeof(multitrack_option_selector_t));

	//prepare option selector window
	WINDOW *option_win = newwin(6, 30, 0, 0);
	wattrset(option_win, COLOR_PAIR(1)|A_REVERSE);
	werase(option_win);
	wrefresh(option_win);
	option_selector->window = option_win;

	//prepare ITEMs
	ITEM *options[NUM_OPTIONS] =  {new_item("Track satellite", ""),
				      new_item("Predict passes", ""),
				      new_item("Predict visible passes", ""),
				      new_item("Display orbital data", ""),
				      new_item("Show transponders", ""),
				      new_item("Solar illumination prediction", ""),
				      NULL};

	int item_types[NUM_OPTIONS] = {OPTION_SINGLETRACK,
				      OPTION_PREDICT,
				      OPTION_PREDICT_VISIBLE,
				      OPTION_DISPLAY_ORBITAL_DATA,
				      OPTION_EDIT_TRANSPONDER,
				      OPTION_SOLAR_ILLUMINATION};
	option_selector->items = (ITEM**)malloc(sizeof(ITEM*)*NUM_OPTIONS);
	option_selector->item_types = (int*)malloc(sizeof(int)*NUM_OPTIONS);
	for (int i=0; i < NUM_OPTIONS; i++) {
		option_selector->items[i] = options[i];
		option_selector->item_types[i] = item_types[i];
		set_item_userptr(option_selector->items[i], &(option_selector->item_types[i]));
	}

	//prepare MENU
	MENU *menu = new_menu(option_selector->items);
	set_menu_back(menu,COLOR_PAIR(1)|A_REVERSE);
	set_menu_fore(menu,COLOR_PAIR(4)|A_REVERSE);
	set_menu_win(menu, option_win);

	int max_width, max_height;
	getmaxyx(option_win, max_height, max_width);
	option_selector->sub_window = derwin(option_win, max_height, max_width-1, 0, 1);
	set_menu_sub(menu, option_selector->sub_window);
	set_menu_format(menu, max_height, 1);

	set_menu_mark(menu, "");
	post_menu(menu);
	option_selector->menu = menu;
	option_selector->visible = false;
	option_selector->option_selected = false;
	return option_selector;
}

void multitrack_option_selector_hide(multitrack_option_selector_t *option_selector)
{
	option_selector->visible = false;
	wbkgd(option_selector->window, COLOR_PAIR(1));
	werase(option_selector->window);
	wrefresh(option_selector->window);
}

void multitrack_option_selector_show(multitrack_option_selector_t *option_selector)
{
	option_selector->visible = true;
}

bool multitrack_option_selector_visible(multitrack_option_selector_t *option_selector)
{
	return option_selector->visible;
}

void multitrack_option_selector_display(int row, multitrack_option_selector_t *option_selector)
{
	if (option_selector->visible) {
		mvwin(option_selector->window, row, 2);
		wbkgd(option_selector->window, COLOR_PAIR(4)|A_REVERSE);
		unpost_menu(option_selector->menu);
		post_menu(option_selector->menu);
		wrefresh(option_selector->window);
	}
}

bool multitrack_option_selector_handle(multitrack_option_selector_t *option_selector, int input_key)
{
	bool handled = false;
	switch (input_key) {
		case KEY_RIGHT:
		case 10:
			option_selector->option_selected = true;
		case 'q':
		case 27:
		case KEY_LEFT:
			multitrack_option_selector_hide(option_selector);
			handled = true;
			break;
		case KEY_UP:
			menu_driver(option_selector->menu, REQ_UP_ITEM);
			handled = true;
			break;
		case KEY_DOWN:
			menu_driver(option_selector->menu, REQ_DOWN_ITEM);
			handled = true;
			break;
	}
	return handled;
}

int multitrack_option_selector_get_option(multitrack_option_selector_t *option_selector)
{
	int option = *((int*)item_userptr(current_item(option_selector->menu)));
	return option;
}

#define SEARCH_FIELD_HEIGHT 3
#define SEARCH_FIELD_LENGTH 78
#define FIELD_START_COL 29
#define FIELD_LENGTH (SEARCH_FIELD_LENGTH - FIELD_START_COL)
multitrack_searcher_t *multitrack_searcher_create(int row, int col)
{
	multitrack_searcher_t *searcher = (multitrack_searcher_t*)malloc(sizeof(multitrack_searcher_t));
	searcher->window = newwin(SEARCH_FIELD_HEIGHT, SEARCH_FIELD_LENGTH, row, col);
	searcher->visible = false;

	//prepare form and field
	searcher->field = (FIELD**)malloc(sizeof(FIELD*)*2);
	searcher->field[0] = new_field(1, FIELD_LENGTH, 0, 0, 0, 0);
	searcher->field[1] = NULL;
	searcher->form = new_form(searcher->field);
	field_opts_off(searcher->field[0], O_AUTOSKIP);
	int rows, cols;
	scale_form(searcher->form, &rows, &cols);
	set_form_win(searcher->form, searcher->window);
	set_form_sub(searcher->form, derwin(searcher->window, rows, cols, 0, FIELD_START_COL));
	keypad(searcher->window, TRUE);
	post_form(searcher->form);

	//prepare search match control
	searcher->matches = NULL;
	multitrack_searcher_clear_matches(searcher);

	return searcher;
}

void multitrack_searcher_show(multitrack_searcher_t *search_field)
{
	search_field->visible = true;
	multitrack_searcher_display(search_field);
}

bool multitrack_searcher_visible(multitrack_searcher_t *search_field)
{
	return search_field->visible;
}

void multitrack_searcher_display(multitrack_searcher_t *search_field)
{
	if (search_field->visible) {
		//fill window without destroying the styling of the FORM :^)
		wattrset(search_field->window, COLOR_PAIR(1));
		mvwprintw(search_field->window, 1, 0, "%-78s", "");
		mvwprintw(search_field->window, 2, 0, "%-78s", "");

		//print keybindings in the same style as htop
		int col = 0;
		wattrset(search_field->window, COLOR_PAIR(1));
		mvwprintw(search_field->window, 0, col, "F3");
		col+=2;
		wattrset(search_field->window, COLOR_PAIR(4)|A_REVERSE);
		mvwprintw(search_field->window, 0, col, "Next   ");
		col+=6;
		wattrset(search_field->window, COLOR_PAIR(1));
		mvwprintw(search_field->window, 0, col, "Esc");
		col+=3;
		wattrset(search_field->window, COLOR_PAIR(4)|A_REVERSE);
		mvwprintw(search_field->window, 0, col, "Cancel ");
		col+=8;
		wattrset(search_field->window, COLOR_PAIR(1));
		mvwprintw(search_field->window, 0, col, " ");
		col+=1;
		wattrset(search_field->window, COLOR_PAIR(4)|A_REVERSE);
		mvwprintw(search_field->window, 0, col, " Search: ");
		col+=9;

		//update form colors
		set_field_back(search_field->field[0], search_field->attributes);
		form_driver(search_field->form, REQ_VALIDATION);
		wrefresh(search_field->window);
	}
}

void multitrack_searcher_hide(multitrack_searcher_t *search_field)
{
	form_driver(search_field->form, REQ_CLR_FIELD);
	search_field->visible = false;
	multitrack_searcher_clear_matches(search_field);
}

void multitrack_searcher_match(multitrack_searcher_t *search_field, bool match)
{
	if (match) {
		search_field->attributes = COLOR_PAIR(4)|A_REVERSE;
	} else {
		search_field->attributes = COLOR_PAIR(7)|A_REVERSE;
	}
}

void multitrack_searcher_destroy(multitrack_searcher_t **search_field)
{
	multitrack_searcher_clear_matches(*search_field);
	free_field((*search_field)->field[0]);
	free((*search_field)->field);
	free_form((*search_field)->form);
	free(*search_field);
	delwin((*search_field)->window);
	*search_field = NULL;
}

bool multitrack_searcher_handle(multitrack_searcher_t *search_field, int input_key)
{
	bool character_handled = false;
	char *expression = NULL;
	switch (input_key) {
		case KEY_BACKSPACE:
			expression = multitrack_searcher_string(search_field);
			if (strlen(expression) == 0) {
				//hide search field if it already is empty
				multitrack_searcher_hide(search_field);
			} else {
				//delete characters
				form_driver(search_field->form, REQ_DEL_PREV);
				form_driver(search_field->form, REQ_VALIDATION);
			}
			free(expression);
			break;
		default:
			form_driver(search_field->form, input_key);
			form_driver(search_field->form, REQ_VALIDATION);
			character_handled = true;
			break;
	}
	return character_handled;
}

void trim_whitespaces_from_end(char *str);

char *multitrack_searcher_string(multitrack_searcher_t *search_field)
{
	char *ret_str = strdup(field_buffer(search_field->field[0], 0));
	trim_whitespaces_from_end(ret_str);
	return ret_str;
}
