ROOT := $(abspath ../..)
BUILD := build
BIN := $(BUILD)/crosswinds_headless
# Each suite that saves gets an emptied directory, so a run never resumes the
# company an earlier run left behind.
FS := $(BUILD)/fs
ifeq ($(origin CXX),default)
CXX := clang++
endif
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -I../host/shim -I$(ROOT)
SRC := harness.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp
HEADERS := economy_fixtures.h $(ROOT)/game.h $(ROOT)/x4_time.h $(ROOT)/sim_voyages.h $(ROOT)/sim_return.h $(ROOT)/sim_merchant.h

all: $(BIN)

$(BIN): $(SRC) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

test: $(BIN)
	./$(BIN) 1000

$(BUILD)/x4_button_events_test: x4_button_events_test.cpp $(ROOT)/x4_button_events.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined $< -o $@

button-events-test: $(BUILD)/x4_button_events_test
	./$(BUILD)/x4_button_events_test

$(BUILD)/x4_standby_test: x4_standby_test.cpp $(ROOT)/x4_standby.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined $< -o $@

standby-test: $(BUILD)/x4_standby_test
	./$(BUILD)/x4_standby_test

$(BUILD)/economy_monte_carlo: economy_monte_carlo.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) economy_monte_carlo.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

economy: $(BUILD)/economy_monte_carlo
	./$(BUILD)/economy_monte_carlo 1000

economy-pilot: $(BUILD)/economy_monte_carlo
	./$(BUILD)/economy_monte_carlo 50

$(BUILD)/economy_regressions: economy_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) economy_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

economy-regressions: $(BUILD)/economy_regressions
	./$(BUILD)/economy_regressions

$(BUILD)/persistence_harness: persistence_harness.cpp $(ROOT)/save.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -DSH_HOST_TESTS -fsanitize=undefined -fno-sanitize-recover=undefined persistence_harness.cpp $(ROOT)/save.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

persistence-test: $(BUILD)/persistence_harness
	rm -rf $(FS)/persistence && mkdir -p $(FS)/persistence
	SH_FS_DIR=$(abspath $(FS)/persistence) ./$(BUILD)/persistence_harness

$(BUILD)/return_regressions: return_regressions.cpp $(ROOT)/save.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -DSH_HOST_TESTS -fsanitize=undefined -fno-sanitize-recover=undefined return_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

return-test: $(BUILD)/return_regressions
	rm -rf $(FS)/return && mkdir -p $(FS)/return
	SH_FS_DIR=$(abspath $(FS)/return) ./$(BUILD)/return_regressions

$(BUILD)/voyage_regressions: voyage_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined voyage_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

voyage-test: $(BUILD)/voyage_regressions
	./$(BUILD)/voyage_regressions

$(BUILD)/manifest_regressions: manifest_regressions.cpp $(ROOT)/save.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -DSH_HOST_TESTS -fsanitize=undefined -fno-sanitize-recover=undefined manifest_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

manifest-test: $(BUILD)/manifest_regressions
	rm -rf $(FS)/manifest && mkdir -p $(FS)/manifest
	SH_FS_DIR=$(abspath $(FS)/manifest) ./$(BUILD)/manifest_regressions

.PHONY: manifest-test

$(BUILD)/merchant_regressions: merchant_regressions.cpp $(ROOT)/save.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -DSH_HOST_TESTS -fsanitize=undefined -fno-sanitize-recover=undefined merchant_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

merchant-test: $(BUILD)/merchant_regressions
	rm -rf $(FS)/merchant && mkdir -p $(FS)/merchant
	SH_FS_DIR=$(abspath $(FS)/merchant) ./$(BUILD)/merchant_regressions

$(BUILD)/merchant_monte_carlo: merchant_monte_carlo.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined merchant_monte_carlo.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

merchant-monte-carlo: $(BUILD)/merchant_monte_carlo
	./$(BUILD)/merchant_monte_carlo 100

.PHONY: merchant-test merchant-monte-carlo

$(BUILD)/voyage_monte_carlo: voyage_monte_carlo.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) voyage_monte_carlo.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

voyage-pilot: $(BUILD)/voyage_monte_carlo
	./$(BUILD)/voyage_monte_carlo 10

$(BUILD)/voyage_quotes_audit: voyage_quotes_audit.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) voyage_quotes_audit.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

voyage-quotes: $(BUILD)/voyage_quotes_audit
	./$(BUILD)/voyage_quotes_audit

$(BUILD)/economy_regressions_ubsan: economy_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined economy_regressions.cpp $(ROOT)/sim.cpp $(ROOT)/content.cpp -o $@

economy-ubsan: $(BUILD)/economy_regressions_ubsan
	./$(BUILD)/economy_regressions_ubsan --exhaustive

.PHONY: button-events-test standby-test persistence-test return-test voyage-test voyage-pilot voyage-quotes economy economy-pilot economy-regressions economy-ubsan

ubsan:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined $(SRC) -o $(BUILD)/crosswinds_ubsan
	./$(BUILD)/crosswinds_ubsan 200

clean:
	rm -rf $(BUILD)

.PHONY: all test ubsan clean
