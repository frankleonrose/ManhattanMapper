class ParameterStore;
class AppState;
class Mode;

bool readParametersFromSD(ParameterStore &pstore);
bool writeParametersToSD(ParameterStore &pstore);
void writeLocation(const AppState &state, const AppState &oldState, Mode *triggeringMode);

void storageSetup();