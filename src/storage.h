class ParameterStore;
class AppState;
template <class TAppState> class Mode;

bool readParametersFromSD(ParameterStore &pstore);
bool writeParametersToSD(ParameterStore &pstore);
void writeLocation(const AppState &state, const AppState &oldState, Mode<AppState> *triggeringMode);

void storageSetup();