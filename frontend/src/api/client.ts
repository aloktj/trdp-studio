export interface ApiError extends Error {
  status?: number;
}

async function handleResponse<T>(response: Response): Promise<T> {
  if (!response.ok) {
    const error: ApiError = new Error('Request failed');
    error.status = response.status;
    let message: string | undefined;
    try {
      const body = await response.json();
      message = body?.error ?? body?.message;
    } catch (err) {
      // ignore JSON parse issues
    }
    if (message) {
      error.message = message;
    }
    throw error;
  }
  if (response.status === 204) {
    return {} as T;
  }
  const data = (await response.json()) as T;
  return data;
}

export async function apiGet<T>(path: string): Promise<T> {
  const res = await fetch(path, {
    credentials: 'include',
    headers: {
      Accept: 'application/json',
    },
  });
  return handleResponse<T>(res);
}

export async function apiPost<T>(path: string, body?: unknown): Promise<T> {
  const res = await fetch(path, {
    method: 'POST',
    credentials: 'include',
    headers: {
      'Content-Type': 'application/json',
      Accept: 'application/json',
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  return handleResponse<T>(res);
}
