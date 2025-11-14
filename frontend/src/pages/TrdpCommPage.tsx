import { useEffect, useState } from 'react';
import type { FormEvent } from 'react';
import { apiGet, apiPost } from '../api/client';
import type { MdMessage, PdMessage } from '../types';

export function TrdpCommPage() {
  const [pdOutgoing, setPdOutgoing] = useState<PdMessage[]>([]);
  const [pdIncoming, setPdIncoming] = useState<PdMessage[]>([]);
  const [mdIncoming, setMdIncoming] = useState<MdMessage[]>([]);
  const [mdSubject, setMdSubject] = useState('');
  const [mdPayload, setMdPayload] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  useEffect(() => {
    refreshData();
  }, []);

  const refreshData = async () => {
    try {
      const [pdOut, pdIn, mdIn] = await Promise.all([
        apiGet<PdMessage[]>('/api/pd/outgoing'),
        apiGet<PdMessage[]>('/api/pd/incoming'),
        apiGet<MdMessage[]>('/api/md/incoming'),
      ]);
      setPdOutgoing(pdOut);
      setPdIncoming(pdIn);
      setMdIncoming(mdIn);
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const handleUpdatePayload = async (messageId: number) => {
    const newPayload = prompt('Enter payload in hex (no spaces)');
    if (!newPayload) {
      return;
    }
    try {
      await apiPost(`/api/pd/outgoing/${messageId}/payload`, { payload_hex: newPayload });
      setNotice('Payload updated');
      await refreshData();
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const handleSendMd = async (event: FormEvent) => {
    event.preventDefault();
    setError(null);
    setNotice(null);
    try {
      await apiPost('/api/md/send', { subject: mdSubject, payload_hex: mdPayload });
      setMdSubject('');
      setMdPayload('');
      setNotice('MD message sent');
      await refreshData();
    } catch (err) {
      setError((err as Error).message);
    }
  };

  const renderPdTable = (title: string, items: PdMessage[] = [], allowUpdate = false) => {
    const columnCount = allowUpdate ? 5 : 4;
    return (
      <section className="card">
        <h2>{title}</h2>
        <table>
          <thead>
            <tr>
              <th>ID</th>
              <th>Name</th>
              <th>Payload</th>
              <th>Updated</th>
              {allowUpdate && <th>Actions</th>}
            </tr>
          </thead>
          <tbody>
            {items.length === 0 ? (
              <tr>
                <td colSpan={columnCount}>No messages</td>
              </tr>
            ) : (
              items.map((msg) => (
                <tr key={msg.id}>
                  <td>{msg.id}</td>
                  <td>{msg.name}</td>
                  <td>
                    <code>{msg.payload_hex}</code>
                  </td>
                  <td>{new Date(msg.updated_at).toLocaleString()}</td>
                  {allowUpdate && (
                    <td>
                      <button type="button" onClick={() => handleUpdatePayload(msg.id)}>
                        Update payload
                      </button>
                    </td>
                  )}
                </tr>
              ))
            )}
          </tbody>
        </table>
      </section>
    );
  };

  return (
    <div>
      <h1>TRDP Communication Monitor</h1>
      {error && <div className="error">{error}</div>}
      {notice && <div className="notice">{notice}</div>}

      {renderPdTable('Process Data - Outgoing', pdOutgoing, true)}
      {renderPdTable('Process Data - Incoming', pdIncoming)}

      <section className="card">
        <h2>Send Message Data</h2>
        <form className="stack" onSubmit={handleSendMd}>
          <label>
            Subject
            <input value={mdSubject} onChange={(e) => setMdSubject(e.target.value)} />
          </label>
          <label>
            Payload (hex)
            <input value={mdPayload} onChange={(e) => setMdPayload(e.target.value)} />
          </label>
          <button type="submit">Send</button>
        </form>
      </section>

      <section className="card">
        <h2>Message Data - Incoming</h2>
        <table>
          <thead>
            <tr>
              <th>ID</th>
              <th>Subject</th>
              <th>Payload</th>
              <th>Timestamp</th>
            </tr>
          </thead>
          <tbody>
            {mdIncoming.length === 0 ? (
              <tr>
                <td colSpan={4}>No messages</td>
              </tr>
            ) : (
              mdIncoming.map((msg) => (
                <tr key={msg.id}>
                  <td>{msg.id}</td>
                  <td>{msg.subject}</td>
                  <td>
                    <code>{msg.payload_hex}</code>
                  </td>
                  <td>{new Date(msg.timestamp).toLocaleString()}</td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </section>
    </div>
  );
}
