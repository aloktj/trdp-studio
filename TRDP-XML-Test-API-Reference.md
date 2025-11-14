# TRDP XML Test API Reference

This guide centralizes the TRDP APIs used by the XML-driven PD/MD test programs. Each entry lists the official function signature (from the public headers) and explains how the XML test harnesses use it, so you can follow the same pattern when creating your own tooling or simulators.

## 1. XML Document & Configuration Helpers (from `tau_xml.h`)

### `tau_prepareXmlDoc`
```
TRDP_ERR_T tau_prepareXmlDoc(
    const CHAR8 *pFileName,
    TRDP_XML_DOC_HANDLE_T *pDocHnd);
```
Loads an XML device description into a DOM tree and returns a document handle. Both `trdp-xmlpd-test` variants call this first, so all configuration parsing happens on a ready XML DOM.

### `tau_readXmlDeviceConfig`
```
TRDP_ERR_T tau_readXmlDeviceConfig(
    const TRDP_XML_DOC_HANDLE_T *pDocHnd,
    TRDP_MEM_CONFIG_T *pMemConfig,
    TRDP_DBG_CONFIG_T *pDbgConfig,
    UINT32 *pNumComPar, TRDP_COM_PAR_T **ppComPar,
    UINT32 *pNumIfConfig, TRDP_IF_CONFIG_T **ppIfConfig);
```
Parses `<device-configuration>` and retrieves:
- memory pools
- debug configuration
- COM parameter table
- per-interface defaults

PD tests pass this directly to `tlc_init()` and later to `tlc_openSession()`.

### `tau_readXmlDatasetConfig`
```
TRDP_ERR_T tau_readXmlDatasetConfig(
    const TRDP_XML_DOC_HANDLE_T *pDocHnd,
    UINT32 *pNumComId,
    TRDP_COMID_DSID_MAP_T **ppComIdDsIdMap,
    UINT32 *pNumDataset,
    papTRDP_DATASET_T papDataset);
```
Reads dataset definitions and the ComID↔Dataset mapping. Used by `initMarshalling()` to initialize the marshaller so payloads can be packed according to XML definitions.

### `tau_readXmlInterfaceConfig`
```
TRDP_ERR_T tau_readXmlInterfaceConfig(
    const TRDP_XML_DOC_HANDLE_T *pDocHnd,
    const CHAR8 *pIfName,
    TRDP_PROCESS_CONFIG_T *pProcessConfig,
    TRDP_PD_CONFIG_T *pPdConfig,
    TRDP_MD_CONFIG_T *pMdConfig,
    UINT32 *pNumExchgPar,
    TRDP_EXCHG_PAR_T **ppExchgPar);
```
Reads `<telegram>` entries for an interface and provides all PD/MD defaults. Each TRDP session uses this to populate its publish/subscribe list.

### `tau_freeTelegrams`
```
void tau_freeTelegrams(UINT32 numExchgPar, TRDP_EXCHG_PAR_T *pExchgPar);
```
Frees telegram arrays created by `tau_readXmlInterfaceConfig()` once the entries are converted into runtime PD publishers/subscribers.

### `tau_freeXmlDoc`
```
void tau_freeXmlDoc(TRDP_XML_DOC_HANDLE_T *pDocHnd);
```
Destroys the DOM document created by `tau_prepareXmlDoc()`. Used when no more XML parsing is required.

## 2. Marshalling Helpers (from `tau_marshall.h`)

### `tau_initMarshall`
```
TRDP_ERR_T tau_initMarshall(
    void **ppRefCon,
    UINT32 numComId,
    TRDP_COMID_DSID_MAP_T *pComIdDsIdMap,
    UINT32 numDataSet,
    TRDP_DATASET_T *pDataset[]);
```
Initializes the marshalling context used by PD send/receive paths. The returned reference is stored in `TRDP_MARSHALL_CONFIG_T.pRefCon`.

### `tau_marshall` / `tau_unmarshall`
```
TRDP_ERR_T tau_marshall(
    void *pRefCon, UINT32 comId,
    const UINT8 *pSrc, UINT32 srcSize,
    UINT8 *pDest, UINT32 *pDestSize,
    TRDP_DATASET_T **ppDSPointer);

TRDP_ERR_T tau_unmarshall(
    void *pRefCon, UINT32 comId,
    UINT8 *pSrc, UINT32 srcSize,
    UINT8 *pDest, UINT32 *pDestSize,
    TRDP_DATASET_T **ppDSPointer);
```
Serialize/deserialize PD telegram payloads using XML dataset definitions. The test harness attaches these callbacks in the `TRDP_MARSHALL_CONFIG_T`.

## 3. Session Lifecycle APIs (from `trdp_if_light.h`)

### `tlc_init`
```
TRDP_ERR_T tlc_init(
    TRDP_PRINT_DBG_T pPrintDebugString,
    void *pRefCon,
    const TRDP_MEM_CONFIG_T *pMemConfig);
```
Initializes the TRDP stack. Called once per program, after reading device-level XML configuration.

### `tlc_openSession`
```
TRDP_ERR_T tlc_openSession(
    TRDP_APP_SESSION_T *pAppHandle,
    TRDP_IP_ADDR_T ownIpAddr,
    TRDP_IP_ADDR_T leaderIpAddr,
    const TRDP_MARSHALL_CONFIG_T *pMarshall,
    const TRDP_PD_CONFIG_T *pPdDefault,
    const TRDP_MD_CONFIG_T *pMdDefault,
    const TRDP_PROCESS_CONFIG_T *pProcessConfig);
```
Creates one TRDP session per `<bus-interface>`. Each session then publishes/subscribes telegrams declared in that interface.

### `tlc_updateSession`
```
TRDP_ERR_T tlc_updateSession(TRDP_APP_SESSION_T appHandle);
```
Used by the “fast” test variant to update runtime tables after creating publishers/subscribers.

### `tlc_process`
```
TRDP_ERR_T tlc_process(
    TRDP_APP_SESSION_T appHandle,
    TRDP_FDS_T *pRfds,
    INT32 *pCount);
```
The single-threaded processing loop: handles cyclic send/receive when no dedicated threads are used.

### `tlc_closeSession` / `tlc_terminate`
```
TRDP_ERR_T tlc_closeSession(TRDP_APP_SESSION_T appHandle);
TRDP_ERR_T tlc_terminate(void);
```
Gracefully shuts down all sessions and terminates the TRDP library.

## 4. PD Telegram APIs (from `trdp_if_light.h`)

### `tlp_publish`
```
TRDP_ERR_T tlp_publish(
    TRDP_APP_SESSION_T appHandle,
    TRDP_PUB_T *pPubHandle,
    void *pUserRef,
    TRDP_PD_CALLBACK_T pfCbFunction,
    UINT32 serviceId,
    UINT32 comId,
    UINT32 etbTopoCnt,
    UINT32 opTrnTopoCnt,
    TRDP_IP_ADDR_T srcIpAddr,
    TRDP_IP_ADDR_T destIpAddr,
    UINT32 interval,
    UINT32 redId,
    TRDP_FLAGS_T pktFlags,
    const TRDP_SEND_PARAM_T *pSendParam,
    const UINT8 *pData,
    UINT32 dataSize);
```
Creates a PD publisher from a `<destination>` entry. The returned `pPubHandle` is later used with `tlp_put()`.

### `tlp_subscribe`
```
TRDP_ERR_T tlp_subscribe(
    TRDP_APP_SESSION_T appHandle,
    TRDP_SUB_T *pSubHandle,
    void *pUserRef,
    TRDP_PD_CALLBACK_T pfCbFunction,
    UINT32 serviceId,
    UINT32 comId,
    UINT32 etbTopoCnt,
    UINT32 opTrnTopoCnt,
    TRDP_IP_ADDR_T srcIpAddr1,
    TRDP_IP_ADDR_T srcIpAddr2,
    TRDP_IP_ADDR_T destIpAddr,
    TRDP_FLAGS_T pktFlags,
    const TRDP_COM_PARAM_T *pRecParams,
    UINT32 timeout,
    TRDP_TO_BEHAVIOR_T toBehavior);
```
Creates a PD subscriber from `<source>` entries or multicast sinks. The returned handle is used in `tlp_get()`.

### `tlp_put`
```
TRDP_ERR_T tlp_put(
    TRDP_APP_SESSION_T appHandle,
    TRDP_PUB_T pubHandle,
    const UINT8 *pData,
    UINT32 dataSize);
```
Updates a publisher’s payload buffer and schedules its transmission cycle.

### `tlp_get`
```
TRDP_ERR_T tlp_get(
    TRDP_APP_SESSION_T appHandle,
    TRDP_SUB_T subHandle,
    TRDP_PD_INFO_T *pPdInfo,
    UINT8 *pData,
    UINT32 *pDataSize);
```
Reads the latest payload from a subscribed telegram.

### `tlp_unpublish` / `tlp_unsubscribe`
```
TRDP_ERR_T tlp_unpublish(TRDP_APP_SESSION_T appHandle, TRDP_PUB_T pubHandle);
TRDP_ERR_T tlp_unsubscribe(TRDP_APP_SESSION_T appHandle, TRDP_SUB_T subHandle);
```
Removes publishers and subscribers at shutdown.

### `tlp_getInterval` / `tlp_processReceive` / `tlp_processSend`
```
TRDP_ERR_T tlp_getInterval(
    TRDP_APP_SESSION_T appHandle,
    TRDP_TIME_T *pInterval,
    TRDP_FDS_T *pFileDesc,
    TRDP_SOCK_T *pNoDesc);

TRDP_ERR_T tlp_processReceive(
    TRDP_APP_SESSION_T appHandle,
    TRDP_FDS_T *pRfds,
    INT32 *pCount);

TRDP_ERR_T tlp_processSend(TRDP_APP_SESSION_T appHandle);
```
Used by the “fast” test to run TX/RX loops in dedicated threads:
- `tlp_getInterval()` → prepares `select()` timeout
- `tlp_processReceive()` → handles inbound packets
- `tlp_processSend()` → handles periodic transmit engine

## 5. Typical Call Order (Summary)

A valid XML-driven TRDP application must follow this sequence:

1. **Parse XML**
   - `tau_prepareXmlDoc()`
   - `tau_readXmlDeviceConfig()`
   - `tau_readXmlDatasetConfig()`
   - `tau_readXmlInterfaceConfig()`
2. **Initialize TRDP**
   - `tlc_init()`
   - `tau_initMarshall()`
   - Build `TRDP_MARSHALL_CONFIG_T` with marshall/unmarshall callbacks
3. **Create sessions**
   - `tlc_openSession()` once per bus interface
   - (fast) `tlc_updateSession()`
4. **Create publishers/subscribers**
   - `tlp_publish()` for each destination
   - `tlp_subscribe()` for each source
5. **Run the loop**
   - Single-threaded → `tlc_process()`
   - Multi-threaded → `tlp_getInterval()`, `tlp_processReceive()`, `tlp_processSend()`
6. **Shutdown**
   - `tlp_unpublish()`, `tlp_unsubscribe()`
   - `tlc_closeSession()`, `tlc_terminate()`
   - `tau_freeTelegrams()`, `tau_freeXmlDoc()`
