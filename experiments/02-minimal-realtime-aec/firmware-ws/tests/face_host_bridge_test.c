#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_animator.h"
#include "face_host_bridge.h"

int main(void)
{
    assert(stackchan_face_animator_size() == sizeof(face_animator_t));
    assert(stackchan_face_animator_size() <= 64);
    assert(stackchan_face_state_size() == sizeof(face_animator_state_t));
    assert(stackchan_face_geometry_size() == sizeof(face_geometry_t));
    assert(stackchan_face_algorithm_state_size("envelope") ==
           sizeof(face_animator_t));
    assert(stackchan_face_algorithm_state_size("viseme") > 0);
    assert(stackchan_face_algorithm_state_size("missing") == 0);
    assert(stackchan_face_host_create_algorithm(
               "missing", 16000, NULL, 0) == NULL);

    stackchan_face_host_t *host = stackchan_face_host_create(16000);
    assert(host != NULL);
    assert(stackchan_face_host_algorithm_state_size(host) ==
           sizeof(face_animator_t));
    assert(strcmp(
               stackchan_face_host_algorithm_name(host),
               "envelope") == 0);

    int16_t pcm[320];
    for (size_t index = 0; index < 320; ++index) {
        pcm[index] = index % 24 < 12 ? 10000 : -10000;
    }
    stackchan_face_host_push_pcm(host, pcm, 320);

    face_animator_state_t state;
    face_geometry_t geometry;
    stackchan_face_host_snapshot(host, 320, 240, &state, &geometry);
    assert(state.playout_samples == 320);
    assert(state.speaking);
    assert(geometry.mouth_height > 4);

    stackchan_face_host_destroy(host);
    puts("face_host_bridge_test: PASS");
    return 0;
}
