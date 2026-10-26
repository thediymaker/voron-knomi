# Build and run the desktop simulator from the repo root: make -C sim run
ROOT  := ..
LVGL  := $(ROOT)/.pio/libdeps/knomiv2/lvgl
BUILD := build
CFLAGS   := -O1 -g -DLV_CONF_INCLUDE_SIMPLE -I$(ROOT)/include -I$(LVGL) -I$(ROOT)/ui -w
CXXFLAGS := $(CFLAGS) -std=gnu++17

LV_SRC := $(shell find $(LVGL)/src -name '*.c')
LV_OBJ := $(patsubst $(LVGL)/%.c,$(BUILD)/lvgl/%.o,$(LV_SRC))
UI_SRC := $(wildcard $(ROOT)/ui/*.cpp) sim.cpp
UI_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(UI_SRC)))

$(BUILD)/sim: $(LV_OBJ) $(UI_OBJ)
	$(CXX) -o $@ $^ -lm

$(BUILD)/lvgl/%.o: $(LVGL)/%.c $(ROOT)/include/lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/ui/%.cpp $(wildcard $(ROOT)/ui/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sim.o: sim.cpp $(wildcard $(ROOT)/ui/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mock: $(LV_OBJ) $(BUILD)/mockups.o
	$(CXX) -o $@ $^ -lm

$(BUILD)/mockups.o: mockups.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

mock: $(BUILD)/mock
	cd $(ROOT) && rm -rf sim/out/mock && mkdir -p sim/out/mock && sim/$(BUILD)/mock && /root/.pio-venv/bin/python sim/render.py sim/out/mock

run: $(BUILD)/sim
	cd $(ROOT) && rm -rf sim/out && mkdir -p sim/out && sim/$(BUILD)/sim && /root/.pio-venv/bin/python sim/render.py

clean:
	rm -rf $(BUILD) out

.PHONY: run mock clean
