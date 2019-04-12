#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

//definin a macro function
#ifndef MIN
#define MIN(_x,_y) ((_x) < (_y)) ? (_x) : (_y)
#endif


/////////////////////////////////////MAINS STRUCTs & FUNCTIONS//////////////////////////////////

//station basic struct definition
struct station
{

  //waiting passanger in station
  int waiting_on_station_passengers;

  //entered in train passenger (but not seated)
  int in_train_passengers;

  //used to implement any critical section needed
  pthread_mutex_t lock;

  //condition variables needed
  pthread_cond_t train_arrived_cond;
  pthread_cond_t passengers_seated_cond;
  pthread_cond_t train_is_full_cond;

};


//Initializes a given station struct
void station_init(struct station *station)
{

  station->waiting_on_station_passengers = 0;
  station->in_train_passengers = 0;

  pthread_mutex_init(&(station->lock), NULL);

  pthread_cond_init(&(station->train_arrived_cond), NULL);
  pthread_cond_init(&(station->passengers_seated_cond), NULL);
  pthread_cond_init(&(station->train_is_full_cond), NULL);

}


//Loads the train with passengers.
//When a passenger robot arrives in a station, it first invokes this function (from wait for train function).
void station_load_train(struct station *station, int count)
{

  // Enter critical region
  pthread_mutex_lock(&(station->lock));

  while ((station->waiting_on_station_passengers > 0) && (count > 0)){

    //signal train arrived condition
    //when having free seats and free there is waiting passegengers on station
    pthread_cond_signal(&(station->train_arrived_cond));

    //decrement waiting passengers in station
  	count--;

    //wait for passenger to get seated
  	pthread_cond_wait(&(station->passengers_seated_cond), &(station->lock));

  }

  //wait until train is full all in train passegers get seated
  if (station->in_train_passengers > 0)
  	pthread_cond_wait(&(station->train_is_full_cond), &(station->lock));

  // Leave critical region
  pthread_mutex_unlock(&(station->lock));

}


//This function must not return until a train is in the station
//and there are enough free seats on the train for this passenger.
void station_wait_for_train(struct station *station)
{

  //Enter critical section
  pthread_mutex_lock(&(station->lock));

  //increase waiting passgengers on station
  station->waiting_on_station_passengers++;

  //keep waiting for train to arrive
  pthread_cond_wait(&(station->train_arrived_cond), &(station->lock));

  //once finishing waiting add them as passengers in train
  station->waiting_on_station_passengers--;
  station->in_train_passengers++;

  //leave critical section
  pthread_mutex_unlock(&(station->lock));

  //the current passenger can seat now
  pthread_cond_signal(&(station->passengers_seated_cond));

}


//Use this function to let the train know that the passenger is on board
void station_on_board(struct station *station)
{
  //enter critical section
  pthread_mutex_lock(&(station->lock));

  //the current passenger has taken a seat in train
  station->in_train_passengers--;

  //leave critical section
  pthread_mutex_unlock(&(station->lock));

  //the train will be full when all in passengers have been seated
  //in train passenger can be at max = train seats available
  if (station->in_train_passengers == 0)
  	pthread_cond_broadcast(&(station->train_is_full_cond));

}


/////////////////////////////////////HELPER FUNCTIONS (for testing)//////////////////////////////////

// Count of passenger threads that have completed
 int threads_completed = 0;
 int passenger_numbs = 0;

void* passenger_thread(void *arg)
{
	struct station *station = (struct station*)arg;
	station_wait_for_train(station);
	 printf("Passenger %d has completed waiting for train!\n",++passenger_numbs);
	__sync_add_and_fetch(&threads_completed, 1);

	return NULL;

}

struct load_train_args
{
	struct station *station;
	int free_seats;

};

int load_train_returned = 0;
void* load_train_thread(void *args)
{

	struct load_train_args *ltargs = (struct load_train_args*)args;
	station_load_train(ltargs->station, ltargs->free_seats);
	load_train_returned = 1;
	return NULL;

}


int main()
{
    ///////////////////////////////////////////initialize new station///////////////////////////////////////////////////////////
    struct station station;
	station_init(&station);

	///////////////////////////////////////////Test Cases///////////////////////////////////////////////////////////
    //total passengers in station
    const int current_station_passengers = 50;
    //MAx train seats
    const int max_free_seats_per_train = 30;

	// Create 'passengers' each in their own thread
	int i;
	for (i = 0; i < current_station_passengers; i++) {
		pthread_t tid;
		int ret = pthread_create(&tid, NULL, passenger_thread, &station);
		if (ret != 0) {
			// If this fails, perhaps we exceeded some system limit.
			perror("pthread_create: Try reducing number of passenger threads!");
			exit(1);
		}
	}

	int passengers_left_in_station = current_station_passengers;
	int total_passengers_boarded = 0;

	int pass = 0;
	int train_numbers = 0;
	while (passengers_left_in_station > 0) {

        //int free_seats = 1;
		int free_seats = random() % max_free_seats_per_train;

		//New Train enters a station
		printf("Train %d entering station with %d free seats\n",++train_numbers,free_seats);

		load_train_returned = 0;
		struct load_train_args args = { &station, free_seats };
		pthread_t lt_tid;
		int ret = pthread_create(&lt_tid, NULL, load_train_thread, &args);
		if (ret != 0) {
			perror("pthread_create: Train Thread not created");
			exit(1);
		}

		int threads_passengers_to_enter_this_train = MIN(passengers_left_in_station, free_seats);
		int threads_passengers_entered_this_train = 0;
		while (threads_passengers_entered_this_train < threads_passengers_to_enter_this_train) {

			if (load_train_returned) {
				fprintf(stderr, "Error: station_load_train returned early!\n");
				exit(1);
			}

			if (threads_completed > 0) {
				if ((pass % 2) == 0)
					usleep(random() % 2);
				threads_passengers_entered_this_train++;
				station_on_board(&station);
				__sync_sub_and_fetch(&threads_completed, 1);
			}
		}

		// Wait a little bit longer. Give station_load_train() a chance to return
		for (i = 0; i < 1000; i++) {
			if (i > 50 && load_train_returned)
				break;
			usleep(1000);
		}

		if (!load_train_returned) {
			fprintf(stderr, "Error: station_load_train failed to return\n");
			exit(1);
		}

		while (threads_completed > 0) {
			threads_passengers_entered_this_train++;
			__sync_sub_and_fetch(&threads_completed, 1);
		}

		passengers_left_in_station -= threads_passengers_entered_this_train;
		total_passengers_boarded += threads_passengers_entered_this_train;

		printf("Train %d departed station with %d new passenger(s) (expected %d)%s\n",
			train_numbers,threads_passengers_to_enter_this_train, threads_passengers_entered_this_train,
			(threads_passengers_to_enter_this_train != threads_passengers_entered_this_train) ? " *****" : "");

		if (threads_passengers_to_enter_this_train != threads_passengers_entered_this_train) {
			fprintf(stderr, "Error: Too many passengers on this train!\n");
			exit(1);
		}

		pass++;
	}

	//always will enter this condition
	if (total_passengers_boarded == current_station_passengers) {
		printf("Train Automation System is working well!\n");
		return 0;
	}

}
