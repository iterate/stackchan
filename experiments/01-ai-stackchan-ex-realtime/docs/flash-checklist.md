# Flash checklist (CoreS3 Realtime, no SD)

1. [ ] USB‑C data cable connected Mac ↔ StackChan CoreS3  
2. [ ] `local/SC_SecConfig.yaml` has real Wi‑Fi + OpenAI key  
3. [ ] `./scripts/apply-local-config.sh`  
4. [ ] `./scripts/flash.sh` (firmware + filesystem)  
5. [ ] Serial: `SD not found; config loaded from SPIFFS.`  
6. [ ] Serial / LCD shows Wi‑Fi IP  
7. [ ] Avatar: **Please touch**  
8. [ ] Forehead touch → conversation works  
9. [ ] (Optional later) MCP SSE up; enable in ExConfig; re-`uploadfs`  

If upload cannot find a port:

```bash
ls /dev/cu.*
cd upstream/firmware && pio device list
```
